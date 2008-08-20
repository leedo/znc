/*
 * Copyright (C) 2004-2008  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "Utils.h"
#include "MD5.h"
#include <errno.h>
#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#endif /* HAVE_LIBSSL */
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using std::stringstream;

bool CUtils::stdoutIsTTY;

CUtils::CUtils() {}
CUtils::~CUtils() {}

#ifdef __sun
char *strcasestr(const char *big, const char *little) {
	int len;

	if (!little || !big || !little[0]) {
		return (char *) big;
	}

	len = strlen(little);
	while (*big) {
		if (tolower(*big) == tolower(*little)) {
			if (strncasecmp(big, little, len) == 0) {
				return (char *) big;
			}
		}

		big++;
	}

	return NULL;
}
#endif /* __sun */

#ifdef HAVE_LIBSSL
void CUtils::GenerateCert(FILE *pOut, bool bEncPrivKey, const CString& sHost) {
	EVP_PKEY *pKey = NULL;
	X509 *pCert = NULL;
	X509_NAME *pName = NULL;
	int days = 365;

	u_int iSeed = time(NULL);
	int serial = (rand_r(&iSeed) % 9999);

	RSA *pRSA = RSA_generate_key(1024, 0x10001, NULL, NULL);
	if ((pKey = EVP_PKEY_new())) {
		if (!EVP_PKEY_assign_RSA(pKey, pRSA)) {
			EVP_PKEY_free(pKey);
			return;
		}

		PEM_write_RSAPrivateKey(pOut, pRSA, (bEncPrivKey ? EVP_des_ede3_cbc() : NULL), NULL, 0, NULL, NULL);

		if (!(pCert = X509_new())) {
			EVP_PKEY_free(pKey);
			return;
		}

		X509_set_version(pCert, 2);
		ASN1_INTEGER_set(X509_get_serialNumber(pCert), serial);
		X509_gmtime_adj(X509_get_notBefore(pCert), 0);
		X509_gmtime_adj(X509_get_notAfter(pCert), (long)60*60*24*days);
		X509_set_pubkey(pCert, pKey);

		pName = X509_get_subject_name(pCert);

		const char *pLogName = getenv("LOGNAME");
		const char *pHostName = NULL;

		if (!sHost.empty()) {
			pHostName = sHost.c_str();
		}

		if (!pHostName) {
			pHostName = getenv("HOSTNAME");
		}

		if (!pLogName) {
			pLogName = "Unknown";
		}

		if (!pHostName) {
			pHostName = "unknown.com";
		}

		CString sEmailAddr = pLogName;
		sEmailAddr += "@";
		sEmailAddr += pHostName;

		X509_NAME_add_entry_by_txt(pName, "C", MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "ST", MBSTRING_ASC, (unsigned char *)"SomeState", -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "L", MBSTRING_ASC, (unsigned char *)"SomeCity", -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "O", MBSTRING_ASC, (unsigned char *)"SomeCompany", -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "OU", MBSTRING_ASC, (unsigned char *)pLogName, -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "CN", MBSTRING_ASC, (unsigned char *)pHostName, -1, -1, 0);
		X509_NAME_add_entry_by_txt(pName, "emailAddress", MBSTRING_ASC, (unsigned char *)sEmailAddr.c_str(), -1, -1, 0);

		X509_set_subject_name(pCert, pName);

		if (!X509_sign(pCert, pKey, EVP_md5())) {
			X509_free(pCert);
			EVP_PKEY_free(pKey);
			return;
		}

		PEM_write_X509(pOut, pCert);
		X509_free(pCert);
		EVP_PKEY_free(pKey);
	}
}
#endif /* HAVE_LIBSSL */

CString CUtils::GetIP(unsigned long addr) {
	char szBuf[16];
	memset((char*) szBuf, 0, 16);

	if (addr >= (1 << 24)) {
		unsigned long ip[4];
		ip[0] = addr >> 24 & 255;
		ip[1] = addr >> 16 & 255;
		ip[2] = addr >> 8  & 255;
		ip[3] = addr       & 255;
		sprintf(szBuf, "%lu.%lu.%lu.%lu", ip[0], ip[1], ip[2], ip[3]);
	}

	return szBuf;
}

unsigned long CUtils::GetLongIP(const CString& sIP) {
	unsigned long ret;
	char ip[4][4];
	int i;

	i = sscanf(sIP.c_str(), "%3[0-9].%3[0-9].%3[0-9].%3[0-9]",
			ip[0], ip[1], ip[2], ip[3]);
	if (i != 4)
		return 0;

	ret  = atoi(ip[0]) << 24;
	ret += atoi(ip[1]) << 16;
	ret += atoi(ip[2]) << 8;
	ret += atoi(ip[3]) << 0;

	return ret;
}

CString CUtils::GetHashPass() {
	while (true) {
		char* pass = CUtils::GetPass("Enter Password");
		char* pass1 = (char*) malloc(strlen(pass) +1);
		strcpy(pass1, pass);	// Make a copy of this since it is stored in a static buffer and will be overwritten when we fill pass2 below
		memset((char*) pass, 0, strlen(pass));	// null out our pass so it doesn't sit in memory
		char* pass2 = CUtils::GetPass("Confirm Password");
		int iLen = strlen(pass1);

		if (strcmp(pass1, pass2) != 0) {
			CUtils::PrintError("The supplied passwords did not match");
		} else if (!iLen) {
			CUtils::PrintError("You can not use an empty password");
		} else {
			CString sRet((const char*) CMD5(pass1, iLen));
			memset((char*) pass1, 0, iLen);	// null out our pass so it doesn't sit in memory
			memset((char*) pass2, 0, strlen(pass2));	// null out our pass so it doesn't sit in memory
			free(pass1);

			return sRet;
		}

		memset((char*) pass1, 0, iLen);	// null out our pass so it doesn't sit in memory
		memset((char*) pass2, 0, strlen(pass2));	// null out our pass so it doesn't sit in memory
		free(pass1);
	}

	return "";
}

CString CUtils::GetSaltedHashPass(CString& sSalt) {
	sSalt = GetSalt();
	unsigned int uiSaltLength = sSalt.length();
	const char *pSalt = sSalt.c_str();

	while (true) {
		char* pass = CUtils::GetPass("Enter Password");
		char* pass1 = (char *) malloc(strlen(pass) + 1);
		// Make a copy of this since it is stored in a static buffer and will be overwritten when we fill pass2 below
		strcpy(pass1, pass);
		// null out our pass so it doesn't sit in memory
		memset(pass, 0, strlen(pass));
		char* pass2 = CUtils::GetPass("Confirm Password");
		int iLen = strlen(pass1);

		if (strcmp(pass1, pass2) != 0) {
			CUtils::PrintError("The supplied passwords did not match");
		} else if (!iLen) {
			CUtils::PrintError("You can not use an empty password");
		} else {
			// Construct the salted pass
			char *salted_pass = (char *) malloc(iLen + uiSaltLength + 1);
			strcpy(salted_pass, pass1);
			strcpy(salted_pass + iLen, pSalt);

			CString sRet((const char*) CMD5(salted_pass, iLen + uiSaltLength));
			// null out our pass so it doesn't sit in memory
			memset(salted_pass, 0, iLen + uiSaltLength);
			memset(pass1, 0, iLen);
			memset(pass2, 0, strlen(pass2));
			free(salted_pass);
			free(pass1);

			return sRet;
		}

		memset((char*) pass1, 0, iLen);	// null out our pass so it doesn't sit in memory
		memset((char*) pass2, 0, strlen(pass2));	// null out our pass so it doesn't sit in memory
		free(pass1);
	}

	return "";
}

CString CUtils::GetSalt() {
	return CString::RandomString(20);
}

char* CUtils::GetPass(const CString& sPrompt) {
	PrintPrompt(sPrompt);
	return getpass("");
}

bool CUtils::GetBoolInput(const CString& sPrompt, bool bDefault) {
	return CUtils::GetBoolInput(sPrompt, &bDefault);
}

bool CUtils::GetBoolInput(const CString& sPrompt, bool *pbDefault) {
	CString sRet, sDefault;

	if (pbDefault) {
		sDefault = (*pbDefault) ? "yes" : "no";
	}

	GetInput(sPrompt, sRet, sDefault, "yes/no");

	if (sRet.CaseCmp("yes") == 0) {
		return true;
	} else if (sRet.CaseCmp("no") == 0) {
		return false;
	}

	return GetBoolInput(sPrompt, pbDefault);
}

bool CUtils::GetNumInput(const CString& sPrompt, unsigned int& uRet, unsigned int uMin, unsigned int uMax, unsigned int uDefault) {
	if (uMin > uMax) {
		return false;
	}

	CString sDefault = (uDefault != (unsigned int) ~0) ? CString(uDefault) : "";
	CString sNum, sHint;

	if (uMax != (unsigned int) ~0) {
		sHint = CString(uMin) + " to " + CString(uMax);
	} else if (uMin > 0) {
		sHint = CString(uMin) + " and up";
	}

	while (true) {
		GetInput(sPrompt, sNum, sDefault, sHint);
		if (sNum.empty()) {
			return false;
		}

		uRet = atoi(sNum.c_str());

		if ((uRet >= uMin && uRet <= uMax)) {
			break;
		}

		CUtils::PrintError("Number must be " + sHint);
	}

	return true;
}

bool CUtils::GetInput(const CString& sPrompt, CString& sRet, const CString& sDefault, const CString& sHint) {
	CString sExtra;
	CString sInput;
	sExtra += (!sHint.empty()) ? (" (" + sHint + ")") : "";
	sExtra += (!sDefault.empty()) ? (" [" + sDefault + "]") : "";

	PrintPrompt(sPrompt + sExtra);
	char szBuf[1024];
	memset(szBuf, 0, 1024);
	if (fgets(szBuf, 1024, stdin) == NULL) {
		// Reading failed (Error? EOF?)
		PrintError("Error while reading from stdin. Exiting...");
		exit(-1);
	}
	sInput = szBuf;

	if (sInput.Right(1) == "\n") {
		sInput.RightChomp();
	}

	if (sInput.empty()) {
		sRet = sDefault;
	} else {
		sRet = sInput;
	}

	return !sRet.empty();
}

void CUtils::PrintError(const CString& sMessage) {
	if (stdoutIsTTY)
		fprintf(stdout, "\033[1m\033[34m[\033[31m ** \033[34m]\033[39m\033[22m %s\n", sMessage.c_str());
	else
		fprintf(stdout, "[ ** ] %s\n", sMessage.c_str());
	fflush(stdout);
}

void CUtils::PrintPrompt(const CString& sMessage) {
	if (stdoutIsTTY)
		fprintf(stdout, "\033[1m\033[34m[\033[33m ?? \033[34m]\033[39m\033[22m %s: ", sMessage.c_str());
	else
		fprintf(stdout, "[ ?? ] %s: ", sMessage.c_str());
	fflush(stdout);
}

void CUtils::PrintMessage(const CString& sMessage, bool bStrong) {
	if (stdoutIsTTY) {
		if (bStrong)
			fprintf(stdout, "\033[1m\033[34m[\033[33m ** \033[34m]\033[39m\033[22m \033[1m%s\033[22m\n",
					sMessage.c_str());
		else
			fprintf(stdout, "\033[1m\033[34m[\033[33m ** \033[34m]\033[39m\033[22m %s\n",
					sMessage.c_str());
	} else
		fprintf(stdout, "%s\n", sMessage.c_str());

	fflush(stdout);
}

void CUtils::PrintAction(const CString& sMessage) {
	if (stdoutIsTTY)
		fprintf(stdout, "\033[1m\033[34m[\033[32m    \033[34m]\033[39m\033[22m %s... ", sMessage.c_str());
	else
		fprintf(stdout, "%s... ", sMessage.c_str());
	fflush(stdout);
}

void CUtils::PrintStatus(bool bSuccess, const CString& sMessage) {
	if (stdoutIsTTY) {
		if (!sMessage.empty()) {
			if (bSuccess) {
				fprintf(stdout, "%s", sMessage.c_str());
			} else {
				fprintf(stdout, "\033[1m\033[34m[\033[31m %s \033[34m]"
						"\033[39m\033[22m", sMessage.c_str());
			}
		}

		fprintf(stdout, "\r");

		if (bSuccess) {
			fprintf(stdout, "\033[1m\033[34m[\033[32m ok \033[34m]\033[39m\033[22m\n");
		} else {
			fprintf(stdout, "\033[1m\033[34m[\033[31m !! \033[34m]\033[39m\033[22m\n");
		}
	} else {
		if (bSuccess) {
			fprintf(stdout, "%s\n", sMessage.c_str());
		} else {
			if (!sMessage.empty()) {
				fprintf(stdout, "[ %s ]", sMessage.c_str());
			}

			fprintf(stdout, "\n[ !! ]\n");
		}
	}

	fflush(stdout);
}

bool CTable::AddColumn(const CString& sName) {
	for (unsigned int a = 0; a < m_vsHeaders.size(); a++) {
		if (m_vsHeaders[a].CaseCmp(sName) == 0) {
			return false;
		}
	}

	m_vsHeaders.push_back(sName);
	m_msuWidths[sName] = sName.size();

	return true;
}

unsigned int CTable::AddRow() {
	push_back(map<CString, CString>());
	return size() -1;
}

bool CTable::SetCell(const CString& sColumn, const CString& sValue, unsigned int uRowIdx) {
	if (uRowIdx == (unsigned int) ~0) {
		if (!size()) {
			return false;
		}

		uRowIdx = size() -1;
	}

	(*this)[uRowIdx][sColumn] = sValue;

	if (m_msuWidths[sColumn] < sValue.size())
		m_msuWidths[sColumn] = sValue.size();

	return true;
}

bool CTable::GetLine(unsigned int uIdx, CString& sLine) const {
	stringstream ssRet;

	if (!size()) {
		return false;
	}

	if (uIdx == 1) {
		ssRet.fill(' ');
		ssRet << "| ";

		for (unsigned int a = 0; a < m_vsHeaders.size(); a++) {
			ssRet.width(GetColumnWidth(a));
			ssRet << std::left << m_vsHeaders[a];
			ssRet << ((a == m_vsHeaders.size() -1) ? " |" : " | ");
		}

		sLine = ssRet.str();
		return true;
	} else if ((uIdx == 0) || (uIdx == 2) || (uIdx == (size() +3))) {
		ssRet.fill('-');
		ssRet << "+-";

		for (unsigned int a = 0; a < m_vsHeaders.size(); a++) {
			ssRet.width(GetColumnWidth(a));
			ssRet << std::left << "-";
			ssRet << ((a == m_vsHeaders.size() -1) ? "-+" : "-+-");
		}

		sLine = ssRet.str();
		return true;
	} else {
		uIdx -= 3;

		if (uIdx < size()) {
			const map<CString, CString>& mRow = (*this)[uIdx];
			ssRet.fill(' ');
			ssRet << "| ";

			for (unsigned int c = 0; c < m_vsHeaders.size(); c++) {
				ssRet.width(GetColumnWidth(c));
				ssRet << std::left << mRow.at(m_vsHeaders[c]);
				ssRet << ((c == m_vsHeaders.size() -1) ? " |" : " | ");
			}

			sLine = ssRet.str();
			return true;
		}
	}

	return false;
}

/*
bool CTable::Output(std::ostream oOut) {
	stringstream ssSep;

	ssSep << "-+-";

	oOut << endl << ssSep.str() << endl;

	for (unsigned int b = 0; b < size(); b++) {
		map<CString, CString>* pRow = (*this)[b];

		oOut << " | ";

		for (unsigned int c = 0; c < m_vsHeaders.size(); c++) {
			oOut.width(GetColumnWidth(c));
			oOut << (*pRow)[m_vsHeaders[c]];
			oOut << " | ";
		}

		oOut << endl;
	}

	oOut << ssSep.str() << endl;
	return true;
}
*/

unsigned int CTable::GetColumnWidth(unsigned int uIdx) const {
	if (uIdx >= m_vsHeaders.size()) {
		return 0;
	}

	const CString& sColName = m_vsHeaders[uIdx];
	map<CString, unsigned int>::const_iterator it = m_msuWidths.find(sColName);

	if (it == m_msuWidths.end()) {
		// AddColumn() and SetCell() should make sure that we get a value :/
		return 0;
	}
	return it->second;
}


#ifdef HAVE_LIBSSL
CBlowfish::CBlowfish(const CString & sPassword, int iEncrypt, const CString & sIvec) {
	m_iEncrypt = iEncrypt;
	m_ivec = (unsigned char *)calloc(sizeof(unsigned char), 8);
	m_num = 0;

	if (sIvec.length() >= 8) {
		memcpy(m_ivec, sIvec.data(), 8);
	}

	BF_set_key(&m_bkey, sPassword.length(), (unsigned char *)sPassword.data());
}

CBlowfish::~CBlowfish() {
	free(m_ivec);
}

//! output must be freed
unsigned char *CBlowfish::MD5(const unsigned char *input, u_int ilen) {
	unsigned char *output = (unsigned char *)malloc(MD5_DIGEST_LENGTH);
	::MD5(input, ilen, output);
	return output;
}

//! returns an md5 of the CString (not hex encoded)
CString CBlowfish::MD5(const CString & sInput, bool bHexEncode) {
	CString sRet;
	unsigned char *data = MD5((const unsigned char *)sInput.data(), sInput.length());

	if (!bHexEncode) {
		sRet.append((const char *)data, MD5_DIGEST_LENGTH);
	} else {
		for (int a = 0; a < MD5_DIGEST_LENGTH; a++) {
			sRet += g_HexDigits[data[a] >> 4];
			sRet += g_HexDigits[data[a] & 0xf];
		}
	}

	free(data);
	return sRet;
}

//! output must be the same size as input
void CBlowfish::Crypt(unsigned char *input, unsigned char *output, u_int ibytes) {
	BF_cfb64_encrypt(input, output, ibytes, &m_bkey, m_ivec, &m_num, m_iEncrypt);
}

//! must free result
unsigned char * CBlowfish::Crypt(unsigned char *input, u_int ibytes) {
	unsigned char *buff = (unsigned char *)malloc(ibytes);
	Crypt(input, buff, ibytes);
	return buff;
}

CString CBlowfish::Crypt(const CString & sData) {
	unsigned char *buff = Crypt((unsigned char *)sData.data(), sData.length());
	CString sOutput;
	sOutput.append((const char *)buff, sData.length());
	free(buff);
	return sOutput;
}

#endif // HAVE_LIBSSL

