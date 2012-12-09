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

#pragma once
#include <wx/wx.h>
#include <wx/zipstrm.h>

namespace fsutils
{
	// tests if a is subset of b
	bool isSubsetOf(wxFileName a, wxFileName b);
	
	bool CopyDir(wxString sFrom, wxString sTo);
	void CopyFileList(const wxArrayString &filenames, wxFileName targetDir);

	bool RecursiveDelete(const wxString &path);

	bool CreateAllDirs(const wxFileName &dir);

	void ExtractZipArchive(wxInputStream &stream, const wxString &dest);
	void TransferZipArchive(wxInputStream &stream, wxZipOutputStream &out);

	bool CompressZipArchive(wxOutputStream &stream, const wxString &srcDir);
	
	bool SecureFile (const char * filename);
}