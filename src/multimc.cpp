// 
//  Copyright 2012 MultiMC Contributors
// 
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
// 
//        http://www.apache.org/licenses/LICENSE-2.0
// 
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
//

#include "multimc.h"

#include <wx/utils.h>
#include <wx/mstream.h>
#include <wx/cmdline.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/fs_arc.h>
#include <wx/socket.h>
#include <wx/app.h>
#include <wx/sysopt.h>
#include <wx/progdlg.h>
#include <wx/intl.h>

#include "mainwindow.h"

#include "utils/apputils.h"
#include "utils/osutils.h"

#include "filedownloadtask.h"

#include "taskprogressdialog.h"

#ifdef wx29
#include <wx/persist/toplevel.h>
#include <wx/dir.h>
#endif

#include "resources/windowicon.h"

IMPLEMENT_APP(MultiMC)

// App
bool MultiMC::OnInit()
{
#if __WXGTK__ || defined MSVC
	// Only works with Linux GCC or MSVC
	wxHandleFatalExceptions();
#endif
	exitAction = EXIT_NORMAL;
	startMode = START_NORMAL;
	updateQuiet = false;
	useProvidedDir = false;

	// This is necessary for the update system since it calls OnInitCmdLine
	// to set up the command line arguments that the update system uses.
	if (!wxApp::OnInit())
		return false;

#if OSX
	{
		wxFileName mmcDir = wxFileName::DirName(wxStandardPaths::Get().GetResourcesDir());
		mmcDir.Normalize();

		if (!mmcDir.DirExists())
			mmcDir.Mkdir(0777, wxPATH_MKDIR_FULL);

		wxSetWorkingDirectory(mmcDir.GetFullPath());
	}
#else
	if (!useProvidedDir)
	{
		wxFileName mmcDir (wxStandardPaths::Get().GetExecutablePath());
		wxSetWorkingDirectory(mmcDir.GetPath());
	}
	else
	{
		// do use provided directory
		wxSetWorkingDirectory(providedDir.GetFullPath());
	}
#endif

	if (!InitAppSettings())
	{
		wxLogError(_("Failed to initialize settings."));
		return false;
	}

	SetAppName(_("MultiMC"));
	InstallLangFiles();
	localeHelper.UpdateLangList();

	// Load language.
	long langID = wxLANGUAGE_UNKNOWN;
	if (settings->GetUseSystemLang())
		langID = wxLocale::GetSystemLanguage();
	else
		langID = settings->GetLanguageID();
	langID = localeHelper.FindClosestMatch(langID);

	// If no matching language is found, use English.
	if (langID == wxLANGUAGE_UNKNOWN)
	{
		langID = wxLANGUAGE_ENGLISH_US;
	}

	if (!localeHelper.SetLanguage((wxLanguage)langID))
	{
		localeHelper.SetLanguage(wxLANGUAGE_ENGLISH_US);
		wxLogError(_("Failed to set language. Language set to English."));
	}
	
	wxString cwd = wxGetCwd();
	if(cwd.Contains("!"))
	{
		wxLogError(_("MultiMC has been started from a path that contains '!':\n%s\nThis would break Minecraft. Please move it to a different place."), cwd.c_str());
		return false;
	}

	wxInitAllImageHandlers();
	wxSocketBase::Initialize();
	
	wxMemoryInputStream iconInput16(multimc16, sizeof(multimc16));
	wxMemoryInputStream iconInput32(multimc32, sizeof(multimc32));
	wxMemoryInputStream iconInput64(multimc64, sizeof(multimc64));
	wxMemoryInputStream iconInput128(multimc128, sizeof(multimc128));
	wxIcon icon16,icon32,icon64,icon128;
	icon16.CopyFromBitmap(wxBitmap(wxImage(iconInput16)));
	icon32.CopyFromBitmap(wxBitmap(wxImage(iconInput32)));
	icon64.CopyFromBitmap(wxBitmap(wxImage(iconInput64)));
	icon128.CopyFromBitmap(wxBitmap(wxImage(iconInput128)));
	AppIcons.AddIcon(icon16);
	AppIcons.AddIcon(icon32);
	AppIcons.AddIcon(icon64);
	AppIcons.AddIcon(icon128);
	
	wxFileSystem::AddHandler(new wxArchiveFSHandler);
	// 	wxFileSystem::AddHandler(new wxMemoryFSHandler);
	
	if (!settings->GetInstDir().DirExists())
		settings->GetInstDir().Mkdir();
	if (!settings->GetModsDir().DirExists())
		settings->GetModsDir().Mkdir();
	
	switch (startMode)
	{
	case START_NORMAL:
		{
			MainWindow *mainWin = new MainWindow();
			mainWin->SetName(wxT("MainWindow"));
			if (!wxPersistenceManager::Get().RegisterAndRestore(mainWin))
			{
				mainWin->CenterOnScreen();
			}
			SetTopWindow(mainWin);
			mainWin->Show();
			mainWin->OnStartup();
			return true;
		}

	case START_LAUNCH_INSTANCE:
		{
			MainWindow *mainWin = new MainWindow();
			mainWin->SetName(wxT("MainWindow"));
			if (!wxPersistenceManager::Get().RegisterAndRestore(mainWin))
			{
				mainWin->CenterOnScreen();
			}
			SetTopWindow(mainWin);
			mainWin->launchInstance = launchInstance;
			mainWin->OnStartup();
			mainWin->Hide();
			return true;
		}

	case START_INSTALL_UPDATE:
		InstallUpdate();
		return false;
	}

	return false;
}

void MultiMC::OnInitCmdLine(wxCmdLineParser &parser)
{
	parser.SetDesc(cmdLineDesc);
	parser.SetSwitchChars("-");
}

bool MultiMC::OnCmdLineParsed(wxCmdLineParser& parser)
{
	wxString parsedOption;
	if(parser.Found("d", &parsedOption))
	{
		if (!wxDirExists(parsedOption))
		{
			std::cerr << "Provided directory doesn't exist!" << std::endl;
			return false;
		}
		providedDir.AssignDir( parsedOption );
		if(!providedDir.IsDirReadable() || !providedDir.IsDirWritable())
		{
			std::cerr << "You don't have read or write rights for the provided directory!" << std::endl;
			return false;
		}
		useProvidedDir = true;
	}
	if (parser.Found("u", &parsedOption))
	{
		updateQuiet = parser.Found("U");

		thisFileName = wxStandardPaths::Get().GetExecutablePath();
		updateTarget = parsedOption;
		startMode = START_INSTALL_UPDATE;
		return true;
	}
	else if(parser.Found("l", &parsedOption))
	{
		launchInstance = parsedOption;
		startMode = START_LAUNCH_INSTANCE;
		return true;
	}
	return true;
}

inline void PulseYieldSleep(int secs, wxProgressDialog *dlg)
{
	int waitLoops = secs * 10;
	for (int i = 0; i < waitLoops; i++)
	{
		wxMilliSleep(100);
		wxYieldIfNeeded();
		dlg->Pulse();
	}
}

const int installUpdateTries = 5;

void MultiMC::InstallUpdate()
{
	int tryNum = 0;
	wxProgressDialog *progDlg = new wxProgressDialog(_("Installing updates..."),
		_("Waiting for old version to exit..."));

Retry:
	tryNum++;
	progDlg->Pulse(wxString::Format(
		_("Waiting for old version to exit... Try #%i."), tryNum));

	// Let the other process exit.
	PulseYieldSleep(1, progDlg);

	wxFileName targetFile(updateTarget);
	
	if (!wxCopyFile(thisFileName, targetFile.GetFullPath()))
	{
		if (tryNum < installUpdateTries)
			goto Retry;
		else
		{
			wxLogError(_("Failed to install updates. Tried %i times. \n\
This is probably because the file that is supposed to be updated is in use. \n\
Please check to make sure there are no other running MultiMC processes \n\
and that MultiMC's updater has sufficient permissions to replace the file \n\
%s and then try again."), tryNum, targetFile.GetFullPath().c_str());
			progDlg->Destroy();
			return;
		}
	}

	progDlg->Pulse(_("Update installed successfully. Starting MultiMC..."));
	progDlg->Fit();
	progDlg->CenterOnScreen();
	wxYieldIfNeeded();
	
	targetFile.MakeAbsolute();

	if (!updateQuiet)
	{
		wxProcess proc;
		wxExecute("\"" + targetFile.GetFullPath() + "\"", wxEXEC_ASYNC, &proc);
		proc.Detach();
	}
	progDlg->Destroy();
}

void MultiMC::YieldSleep(int secs)
{
	int waitLoops = secs * 10;
	for (int i = 0; i < waitLoops; i++)
	{
		wxMilliSleep(100);
		Yield(true);
	}
}

int MultiMC::OnExit()
{
#ifdef WINDOWS
	wxString updaterFileName = "MultiMCUpdate.exe";
#else
	wxString updaterFileName = "MultiMCUpdate";
#endif

	// If we chose to update on exit, download updates.
	if (!updateLaterURL.IsEmpty())
	{
		auto dlTask = new FileDownloadTask(updateLaterURL, 
			wxFileName(updaterFileName), _("Downloading updates..."));
		TaskProgressDialog dlg(NULL);
		if (dlg.ShowModal(dlTask))
			exitAction = EXIT_UPDATE;
		else
		{
			wxLogError(_("Failed to download updates from %s."), 
				updateLaterURL);
		}
	}

	if ((exitAction == EXIT_UPDATE_RESTART || exitAction == EXIT_UPDATE) && 
		wxFileExists(updaterFileName))
	{
		wxFileName updateFile(updaterFileName);
#if LINUX || OSX
			wxExecute("chmod +x " + updateFile.GetFullPath());
			updateFile.MakeAbsolute();
#endif

		wxProcess proc;
		
		wxString updateFilePath = updateFile.GetFullPath();
		wxString thisFilePath = wxStandardPaths::Get().GetExecutablePath();
		wxString additionalArgs;

		if (exitAction == EXIT_UPDATE)
		{
			additionalArgs = "-U";
		}

#if WINDOWS
		wxString launchCmd = wxString::Format("cmd /C %s %s -u \"%s\"",
			updateFilePath.c_str(), additionalArgs.c_str(), thisFilePath.c_str());
#else
		updateFilePath.Replace(" ", "\\ ");
		thisFilePath.Replace(" ", "\\ ");

		wxString launchCmd = wxString::Format("%s -u:%s",
			updateFilePath.c_str(), thisFilePath.c_str());
#endif

		wxExecute(launchCmd, wxEXEC_ASYNC, &proc);
		proc.Detach();
	}
	else if (exitAction == EXIT_RESTART)
	{
		wxProcess proc;

		// Put together the command that MultiMC launched with.
		wxString launchCmd;
		for (int i = 0; i < argc; i++)
		{
			launchCmd += wxString::Format("%s ", argv[i]);
		}

		wxExecute(launchCmd, wxEXEC_ASYNC, &proc);
		proc.Detach();
	}

	delete settings;
	
	return wxApp::OnExit();
}

void MultiMC::OnFatalException()
{
	wxMessageBox(_("A fatal error has occurred and MultiMC has to exit. Sorry for the inconvenience."), 
		_("Oh no!"), wxICON_ERROR | wxCENTER | wxOK);
}

void MultiMC::OnUnhandledException()
{
	OnFatalException();
}

const wxIconBundle &MultiMC::GetAppIcons() const
{
	return AppIcons;
}

const wxString licenseText =
"Copyright 2012 MultiMC Contributors\n\
Licensed under the Apache License, Version 2.0 (the \"License\");\n\
you may not use this file except in compliance with the License.\n\
You may obtain a copy of the License at\n\
\n\
\thttp://www.apache.org/licenses/LICENSE-2.0\n\
\n\
Unless required by applicable law or agreed to in writing, software\n\
distributed under the License is distributed on an \"AS IS\" BASIS,\n\
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n\
See the License for the specific language governing permissions and\n\
limitations under the License.\n\
\n\
MultiMC uses bspatch, \n\
Copyright 2003-2005 Colin Percival\n\
All rights reserved\n\
\n\
Redistribution and use in source and binary forms, with or without\n\
modification, are permitted providing that the following conditions\n\
are met: \n\
1. Redistributions of source code must retain the above copyright\n\
   notice, this list of conditions and the following disclaimer.\n\
2. Redistributions in binary form must reproduce the above copyright\n\
   notice, this list of conditions and the following disclaimer in the\n\
   documentation and/or other materials provided with the distribution.\n\
\n\
THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR\n\
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n\
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n\
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY\n\
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n\
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS\n\
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)\n\
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,\n\
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING\n\
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n\
POSSIBILITY OF SUCH DAMAGE.\n\
\n\
MultiMC also uses the Macify apple API wrapper, \n\
Copyright 2007 Eirik Bjorsnos.\n\
\n\
Licensed under the Apache License, Version 2.0 (the \"License\");\n\
you may not use this file except in compliance with the License.\n\
You may obtain a copy of the License at\n\
\n\
     http://www.apache.org/licenses/LICENSE-2.0\n\
\n\
Unless required by applicable law or agreed to in writing, software\n\
distributed under the License is distributed on an \"AS IS\" BASIS,\n\
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n\
See the License for the specific language governing permissions and\n\
limitations under the License.";

