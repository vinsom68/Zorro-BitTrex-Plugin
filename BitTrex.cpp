/*

https://bittrex.com/home/api

fees https://support.bittrex.com/hc/en-us/articles/115000199651-What-fees-does-Bittrex-charge-

- websockets https://github.com/dhbaird/easywsclient

*/


#define _CRT_SECURE_NO_WARNINGS
#include "stdafx.h"

//#define FROMFILE
//#define FAKELOGIN


INITIALIZE_EASYLOGGINGPP

BOOL APIENTRY DllMain(HMODULE hModule,
DWORD  ul_reason_for_call,
LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		OutputDebugStringA("DllMain, DLL_PROCESS_ATTACH\n");
		break;
	case DLL_THREAD_ATTACH:
		OutputDebugStringA("DllMain, DLL_THREAD_ATTACH\n");
		break;
	case DLL_THREAD_DETACH:
		OutputDebugStringA("DllMain, DLL_THREAD_DETACH\n");
		break;
	case DLL_PROCESS_DETACH:
		OutputDebugStringA("DllMain, DLL_PROCESS_DETACH\n");
		break;
	default:
		OutputDebugStringA("DllMain, ????\n");
		break;
	}
	return TRUE;
}


/////////////////////////////////////////////////////////////
typedef double DATE;
//#include "D:\Zorro\include\trading.h"  // enter your path to trading.h (in your Zorro folder)
#include "..\..\Zorro\include\trading.h"
//#import "C:\\Program Files\\CandleWorks\\FXOrder2Go\fxcore.dll"  // FXCM API module

#define PLUGIN_VERSION	2
#define DLLFUNC extern "C" __declspec(dllexport)
#define LOOP_MS	200	// repeat a failed command after this time (ms)
#define WAIT_MS	10000	// wait this time (ms) for success of a command
//#define CONNECTED (g_pTradeDesk && g_pTradeDesk->IsLoggedIn() != 0)

#define MAX_STRING 1024

/////////////////////////////////////////////////////////////

int(__cdecl *BrokerError)(const char *txt) = NULL;
int(__cdecl *BrokerProgress)(const int percent) = NULL;

//HTTP func pointers prototypes
int(__cdecl *http_send)(char* url, char* data, char* header) = NULL;
long(__cdecl *http_status)(int id) = NULL;
long(__cdecl *http_result)(int id, char* content, long size) = NULL;
int(__cdecl *http_free)(int id) = NULL;


//Wrapper Func pointers declarations
typedef int(__cdecl *BROKEROPEN)(LPCSTR, FARPROC, FARPROC);
typedef int(__cdecl *BROKERTIME)(DATE *);
typedef int(__cdecl *BROKERLOGIN)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
typedef int(__cdecl *BROKERHISTORY2)(LPCSTR, DATE, DATE, int, int, T6*);
typedef int(__cdecl *BROKERASSET)(LPCSTR, double *, double *, double *, double *, double *, double *, double *, double *, double *);
typedef int(__cdecl *BROKERACCOUNT)(LPCSTR, double *, double *, double *);
typedef int(__cdecl *BROKERBUY)(LPCSTR, int, double, double *);
typedef int(__cdecl *BROKERTRADE)(int, double*, double *, double*, double*);
typedef int(__cdecl *BROKERSELL)(int, int);
//typedef int(__cdecl *BROKERSTOP)(int, double);
typedef double(__cdecl *BROKERCOMMAND)(int, DWORD);

static const char * TFARR[] = { "TIME_SERIES_INTRADAY", "TIME_SERIES_DAILY", "TIME_SERIES_WEEKLY", "TIME_SERIES_MONTLY" };

//Timezone info struct
#define pWin32Error(dwSysErr, sMsg )

typedef struct _REG_TZI_FORMAT
{
	LONG Bias;
	LONG StandardBias;
	LONG DaylightBias;
	SYSTEMTIME StandardDate;
	SYSTEMTIME DaylightDate;
} REG_TZI_FORMAT;

struct SymbolCacheItem
{
	std::string json;
	COleDateTime ExpiryTime;
};

struct AssetTicker
{
	double ask;
	double bid;
	double spread;
	double volume;
	double minAmount;
};

//struct SymbolBuyInfo
//{
//	double Amount;
//	double Price;
//};

#define REG_TIME_ZONES "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\"
#define REG_TIME_ZONES_LEN (sizeof(REG_TIME_ZONES)-1)
#define REG_TZ_KEY_MAXLEN (REG_TIME_ZONES_LEN + (sizeof(((TIME_ZONE_INFORMATION*)0)->StandardName)/2) -1)
#define ZONE "Eastern Standard Time" //https://msdn.microsoft.com/en-us/library/ms912391(v=winembedded.11).aspx



enum TIMEFRAME
{
	TIME_SERIES_INTRADAY = 0,
	TIME_SERIES_DAILY = 1,
	TIME_SERIES_WEEKLY = 2,
	TIME_SERIES_MONTLY = 3

};

enum INTERVAL
{
	MIN1 = 1,
	MIN5 = 5,
	MIN15 = 15,
	MIN30 = 30,
	MIN60 = 60,
	MIN240 = 240,
	MIN1440 = 1440,
	MIN10080 = 10080,
	MIN21600 = 21600
};


//////////////////////// DLL Globals
static int g_nLoggedIn = 0;
static HINSTANCE hinstLib = 0;
Dictionary<std::string, SymbolCacheItem> SymbolDic;
Dictionary<std::string, AssetTicker> SymbolLastPrice;
//Dictionary<std::string, double> SymbolBuyInfo;
Dictionary<unsigned int, double> DemoTradesID;
std::string g_PrivateKey = "";
std::string g_PublicKey = "";
std::string g_DisplayCurrency = "";
std::string g_BaseTradeCurrency = "";
picojson::value::object g_AssetPairs;
std::string g_MinOrderSize = "";
int g_DemoTrading = 0;
int g_IntervalSec = 0;
int g_WaitAfterOrderForCheckSec = 0;
int g_EnableLog = 0;
int g_LimitOrderMaxOrderBookDepth = 0;
double g_BaseCurrAccCurrConvRate = 1;
int g_UseCryptoCompareHistory = 0;

std::string _BaseUrl = "https://bittrex.com/api/v1.1/";

////////////////////////////////////////////////////////////////
HINSTANCE LoadDLL()
{
	HINSTANCE hinstLib = 0;
	//hinstLib = LoadLibrary(TEXT("D:\\Zorro\\Plugin\\IB.dll"));
	hinstLib = LoadLibrary(TEXT(".\\Plugin\\IB.dll"));
	if (hinstLib != NULL)
		return hinstLib;
	else
		return 0;
}

//DATE format (OLE date/time) is a double float value, counting days since midnight 30 December 1899, while hours, minutes, and seconds are represented as fractional days. 
DATE convertTime(__time32_t t32)
{
	return (double)t32 / (24.*60.*60.) + 25569.; // 25569. = DATE(1.1.1970 00:00)
}

// number of seconds since January 1st 1970 midnight: 
__time32_t convertTime(DATE date)
{
	return (__time32_t)((date - 25569.)*24.*60.*60.);
}


//https://stackoverflow.com/questions/24752141/hmac-sha512-bug-in-my-code
//https://www.freeformatter.com/hmac-generator.html#ad-output
//http://www.rohitab.com/discuss/topic/39777-hmac-md5sha1/
//http://www.drdobbs.com/security/the-hmac-algorithm/184410908


//http://stackoverflow.com/questions/466071/how-do-i-get-a-specific-time-zone-information-struct-in-win32
int GetTimeZoneInformationByName(TIME_ZONE_INFORMATION *ptzi, const char StandardName[])
{
	int rc;
	HKEY hkey_tz;
	DWORD dw;
	REG_TZI_FORMAT regtzi;
	char tzsubkey[REG_TZ_KEY_MAXLEN + 1] = REG_TIME_ZONES;

	strncpy(tzsubkey + REG_TIME_ZONES_LEN, StandardName, sizeof(tzsubkey) - REG_TIME_ZONES_LEN);
	if (tzsubkey[sizeof(tzsubkey) - 1] != 0) {
		fprintf(stderr, "timezone name too long\n");
		return -1;
	}

	if (ERROR_SUCCESS != (dw = RegOpenKeyA(HKEY_LOCAL_MACHINE, tzsubkey, &hkey_tz))) {
		fprintf(stderr, "failed to open: HKEY_LOCAL_MACHINE\\%s\n", tzsubkey);
		pWin32Error(dw, "RegOpenKey() failed");
		return -1;
	}

	rc = 0;
#define X(param, type, var) \
        do if ((dw = sizeof(var)), (ERROR_SUCCESS != (dw = RegGetValueW(hkey_tz, NULL, param, type, NULL, &var, &dw)))) { \
            fprintf(stderr, "failed to read: HKEY_LOCAL_MACHINE\\%s\\%S\n", tzsubkey, param); \
            pWin32Error(dw, "RegGetValue() failed"); \
            rc = -1; \
            goto ennd; \
						        } while(0)
	X(L"TZI", RRF_RT_REG_BINARY, regtzi);
	X(L"Std", RRF_RT_REG_SZ, ptzi->StandardName);
	X(L"Dlt", RRF_RT_REG_SZ, ptzi->DaylightName);
#undef X
	ptzi->Bias = regtzi.Bias;
	ptzi->DaylightBias = regtzi.DaylightBias;
	ptzi->DaylightDate = regtzi.DaylightDate;
	ptzi->StandardBias = regtzi.StandardBias;
	ptzi->StandardDate = regtzi.StandardDate;
ennd:
	RegCloseKey(hkey_tz);
	return rc;
}


void Log(std::string message, int Level, bool LogZorro)
{

	if (g_EnableLog == 1)
	{
		if (Level == 0)
			LOG(INFO) << message.c_str();
		else if (Level == 1)
			LOG(ERROR) << message.c_str();
	}

	if (LogZorro)
	{
		BrokerError(message.c_str());
	}

}


std::string ftoa(double val)
{
	char cVal[30];
	sprintf(cVal, "%0.8f", val);
	return std::string(cVal);
}

std::string itoa(int val)
{
	char cVal[30];
	sprintf(cVal, "%d", val);
	return std::string(cVal);
}

std::string uitoa(unsigned int val)
{
	char cVal[30];
	sprintf(cVal, "%u", val);
	return std::string(cVal);
}

bool isNumber(std::string line)
{
	char* p;
	strtol(line.c_str(), &p, 10);
	return *p == 0;
}

std::string GetPublicApiKey()
{
	return g_PublicKey;
}

std::string GetNonce()
{
	//TODO
	return "1111";

	SYSTEMTIME st;
	GetSystemTime(&st);
	//printf("\n In C++ : %04d:%02d:%02d:%02d:%02d:%02d:%03d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	st.wYear = st.wYear + 1600;

	//TODO remove
	//st.wHour = st.wHour + 10;

	FILETIME fileTime;
	SystemTimeToFileTime(&st, &fileTime);

	long long ticks = (((ULONGLONG)fileTime.dwHighDateTime) << 32) + fileTime.dwLowDateTime;

	std::string strNonce = "000000000000000000";
	sprintf(&strNonce[0], "%I64u" /*"%llu"*/, ticks);

	return strNonce;

}

//https://stackoverflow.com/questions/31073068/invalid-signature-with-bittrex-api-calls-in-c-sharp
std::string GetAPISignature(std::string ApiPath, std::string PostData, std::string nonce)
{
	
	std::string PublicKey = GetPublicApiKey();
	std::string PrivateKey = g_PrivateKey;
	std::string param = _BaseUrl + ApiPath+ PostData;


	unsigned char hmac_512[SHA512::DIGEST_SIZE];
	memset(hmac_512, 0, SHA512::DIGEST_SIZE);
	std::string encp = base64_decode(PrivateKey);
	HMAC512(PrivateKey, (unsigned char *)param.c_str(), param.length(), hmac_512);

	char buf[2 * SHA512::DIGEST_SIZE + 1];
	buf[2 * SHA512::DIGEST_SIZE] = 0;
	for (int i = 0; i < SHA512::DIGEST_SIZE; i++)
		sprintf(buf + i * 2, "%02x", hmac_512[i]);
	
	std::string ret(buf);
	return ret;

}

std::string GetHeader(std::string ApiPath, std::string PostData, std::string nonce)
{
	std::string sign = GetAPISignature(ApiPath, PostData, nonce);
	std::ostringstream ss;

	ss << "apisign: " << sign.c_str() ;

	return ss.str();

}

std::string GetAsset(char * Asset)
{
	//Remove backslash for currencies
	std::string sAsset = std::string(Asset);
	std::replace(sAsset.begin(), sAsset.end(), '/','-');
	int pos = sAsset.find("-");
	std::string sAssetNew = sAsset.substr(pos+1, sAsset.length()-1 - pos) + "-" + sAsset.substr(0, pos);

	return sAssetNew;
}

unsigned int GetTradeID()
{
	static unsigned int last;
	time_t ltime;
	time(&ltime);
	if (ltime == last)
		ltime++;
	else if (ltime < last)
	{
		ltime = last;
		ltime++;
	}
	last = ltime;
	return ltime;

}

std::string GetTradeIDstring()
{
	unsigned int tid = GetTradeID();
	char temp[20];
	_itoa(tid, temp, 10);
	return std::string(temp);
}



void ClearCache()
{
	SymbolDic.Clear();
}

int AddToCache(std::string url, std::string json)
{
	if (g_IntervalSec <= 0)
		return 0;

	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(url);
		if (jsonCached)
			SymbolDic.Remove(url);

		SymbolCacheItem newitem;
		newitem.json = json;
		COleDateTime currTime = COleDateTime::GetCurrentTime();
		COleDateTimeSpan ts(0,0,0,g_IntervalSec);
		COleDateTime exptime = currTime + ts;
		CString sStart = currTime.Format(_T("%A, %B %d, %Y %H:%M:%S"));
		CString sEnd = exptime.Format(_T("%A, %B %d, %Y %H:%M:%S"));
		newitem.ExpiryTime = exptime;
		SymbolDic.Add(url, newitem);
		return 1;
	}
	catch (...)
	{
		Log("AddToCache Exception", 1,false);
		return 0;
	}
}

int GetFromCache(std::string url, std::string &json)
{
	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(url);
		if (jsonCached)
		{
			COleDateTime currTime = COleDateTime::GetCurrentTime();
			if (!jsonCached->ExpiryTime.GetStatus() == COleDateTime::valid)
				return 0;

			if (currTime > jsonCached->ExpiryTime)
				return 0;
			json = jsonCached->json;
			return 1;
		}
		else
			return 0;
	}
	catch (...)
	{
		Log("GetFromCache Exception", 1,false);
		return 0;
	}
}

void RemoveFromCache(std::string url)
{
	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(url);
		if (jsonCached)
			SymbolDic.Remove(url);
	}
	catch (...)
	{
		Log("RemoveFromCache Exception", 1, false);

	}
}

int CallAPI(std::string ApiPath, std::string& json, std::string symbol, std::string data, bool UseCache, bool UseKrakenRoot=true, bool IsPrivateAPI=false)
{

	int id = 0;
	int length = 0;
	int counter = 0;

	
	std::string URL = "";
	if (UseKrakenRoot)
		URL = _BaseUrl + ApiPath;
	else
		URL = ApiPath;
	std::string CacheKey =URL + "?" + data;
	std::string header="";
	if (IsPrivateAPI)
	{
		std::string nonce = GetNonce();
		if (data == "")
			data = "?";
		else
			data = "?" + data + "&";
		data = data + "apikey=" + GetPublicApiKey() + "&nonce=" + GetNonce();
		header = GetHeader(ApiPath, data,nonce);
		Log(header, 0, false);
	}
	


#ifdef FROMFILE
	//std::ifstream t("..\..\Zorro\json.txt");
	std::ifstream t("json.txt");
	std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
	json.assign(str);
	length = json.length();
#else

	while (length <= 3)
	{
		if (UseCache)
		{
			if (GetFromCache(CacheKey, json))
				return 1;
		}

		if (!IsPrivateAPI)
			id = http_send((char*)URL.c_str(), NULL, NULL);
		else
			id = http_send((char*)(URL + data).c_str(), 0, (char*)header.c_str());
			
		if (!id)
			return 0;
		while (!http_status(id))
		{
			if (!SleepEx(100, FALSE))
				return 0; // wait for the server to reply
		}

		length = http_status(id);
		if (length <= 3)
			http_free(id);

		if (counter > 3)
		{
			Log("Maximum Retry calling http_send reached.", 1,true);
			break;
		}
		counter++;
	}

#endif


	if (length > 3)
	{ //transfer successful?

#ifndef FROMFILE
		string content = (string)malloc(length+1);
		ZeroMemory(content, length );
		http_result(id, content, length ); // store price data in large string
		json.assign(content);
		if (UseCache && json.length()>50)
			AddToCache(CacheKey, content);
		free(content);
#ifdef _DEBUG
		Log(URL, 0, false);
		//Log((char *)json.c_str(),0);
#endif
		http_free(id);
#endif
	}
	else
	{
		BrokerError("Error calling web service");
#ifndef FROMFILE
		http_free(id); //always clean up the id!
#endif
		return 0;
	}

	return 1;
}

int Parse(std::string ApiPath, picojson::value::object& objJson, std::string& json, std::string symbol, std::string data)
{
	try
	{
		picojson::value jsonParsed;
		if (ApiPath.find("/getmarkets") == std::string::npos && ApiPath.find("/GetTicks") == std::string::npos && ApiPath.find("/histo") == std::string::npos  && ApiPath.find("/getorderbook") == std::string::npos )
			Log(json, 0, false);

		std::string err = picojson::parse(jsonParsed, json);
		if (!err.empty())
		{
			Log(err, 1,true);
			return 0;
		}


		// check if the type of the value is "object"
		if (!jsonParsed.is<picojson::object>())
		{
			Log(ApiPath + " - JSON is not an object", 1,true);
			return 0;
		}

		objJson = jsonParsed.get<picojson::object>();

		picojson::value valSuccess;
		int found=objJson.count("success");
		if (found)
		{
			valSuccess = objJson.at("success");
			if (valSuccess.to_str() == "false")
			{
				Log(objJson.at("message").to_str(), 1, true);
				Log(ApiPath, 1, false);
				Log(data, 1, false);

				std::string URL = _BaseUrl + ApiPath;
				std::string CacheKey = URL + "?" + data;
				RemoveFromCache(URL);

				return 0;
			}
		}
		else
		{
			valSuccess = objJson.at("Response");
			if (valSuccess.to_str() != "Success")
			{
				Log(objJson.at("message").to_str(), 1, true);
				Log(ApiPath, 1, false);
				Log(data, 1, false);

				std::string URL = _BaseUrl + ApiPath;
				std::string CacheKey = URL + "?" + data;
				RemoveFromCache(URL);

				return 0;
			}
			
		}


	}
	catch (const std::exception& e)
	{
		Log(e.what(), 0,true);
		return 0;
	}

	return 1;
}

int CallAPIAndParse(std::string ApiPath, picojson::value::object& objJson, std::string symbol, std::string data, bool UseCache, bool UseKrakenRoot = true, bool IsPrivateAPI = false)
{

	try
	{

		std::string json;
		if (!CallAPI(ApiPath, json, symbol, data, UseCache, UseKrakenRoot, IsPrivateAPI))
		{
			Log(ApiPath + " - Error calling API", 1,true);
			return 0;
		}

		if (!Parse(ApiPath, objJson, json, symbol, data))
		{
			Log(ApiPath + " - Error Parsing JSon", 1,true);
			return 0;
		}

	}
	catch (const std::exception& e)
	{
		Log((char *)e.what(), 1, true);
		return 0;
	}

	return 1;
}

/*
0 when the connection to the server was lost, and a new login is required.
1 when the connection is ok, but the market is closed or trade orders are not accepted.
2 when the connection is ok and the market is open for trading at least one of the subscribed assets.
*/
DLLFUNC int BrokerTime(DATE *pTimeGMT);
int IsLoggedIn()
{

#ifdef FAKELOGIN
	return 1;
#endif

	DATE date = 0;
	return g_nLoggedIn;
}

double GetAccBaseCurrExchRate(std::string asset)
{
	Log("GetAccBaseCurrExchRate IN Symbol: " + asset, 0, false);
	double price = 0;


	//int ret = 0;
	std::string ApiPath = "public/getticker?market=" + asset;

	try
	{
		picojson::value::object objJson;
		if (!CallAPIAndParse(ApiPath, objJson, asset, "", false))
		{
			Log("Error calling API GetAccBaseCurrExchRate", 1, true);
			Log("GetAccBaseCurrExchRate OUT", 0, false);
			return 0;
		}

		/*objJson.at("result").get("Last").to_str();
		picojson::value::object::const_iterator iA = objJson.begin(); ++iA;//iA shoud be Time Series Object
		const picojson::value::object& objPair = iA->second.get<picojson::object>();
		std::string sPair = objPair.begin()->first.c_str();

		const picojson::value::array&  askArray = objPair.begin()->second.get("a").get<picojson::array>();
		picojson::value::array::const_iterator iG = askArray.begin();*/
		price = atof(objJson.at("result").get("Last").to_str().c_str());

		if (price == 0)
			Log("No Value for " + asset + " Ticker", 1, true);
	}
	catch (const std::exception& e)
	{
		Log(e.what(), 1, true);
		Log("GetAccBaseCurrExchRate OUT", 0, false);
		return 0;
	}


	Log("GetAccBaseCurrExchRate OUT", 0, false);
	return price;


	//XBT/USD
	//return 3913.889;
}

double CallGetAccBaseCurrExchRate()
{
	Log("CallGetAccBaseCurrExchRate IN", 0, false);
	if (g_BaseTradeCurrency != g_DisplayCurrency)
	{
		std::string asset = GetAsset((char*)(g_BaseTradeCurrency + "/" + g_DisplayCurrency).c_str());
		g_BaseCurrAccCurrConvRate = GetAccBaseCurrExchRate(asset);
		if (g_BaseCurrAccCurrConvRate == 0)
		{
			Log("Error getting exchange rate for " + asset, 1, true);
			Log("CallGetAccBaseCurrExchRate OUT", 0, false);
			return 0;
		}
	}
	else
		g_BaseCurrAccCurrConvRate = 1;

	Log("CallGetAccBaseCurrExchRate OUT", 0, false);
	return g_BaseCurrAccCurrConvRate;
}


int CanSubscribe(std::string& Asset)
{
	Log("CanSubscribe IN", 0,false);

	if (!IsLoggedIn())
		return 0;

	std::string ApiPath = "public/getmarkets ";

	int pos = Asset.find(g_BaseTradeCurrency);
	if (pos >2)
	{
		Log("Base trade currency must be the left one on the pair.", 1,true);
		Log("CanSubscribe OUT", 0,false);
		return 0;
	}


	try
	{
		if (g_AssetPairs.size()==0)
		{
			if (!CallAPIAndParse(ApiPath, g_AssetPairs, Asset, "", true))
			{
				Log("Error calling API CanSubscribe", 1,true);
				Log("CanSubscribe OUT", 0,false);
				return 0;
			}			
		}

		picojson::value::object::const_iterator iA = g_AssetPairs.begin(); ++iA;// ++iA;
		const picojson::value::array& objData = iA->second.get<picojson::array>();
		for (picojson::value::array::const_iterator iC = objData.begin(); iC != objData.end(); ++iC)
		{
			std::string asset= iC->get("MarketName").to_str();
			std::string active = iC->get("IsActive").to_str();

			if (asset==Asset && active == "true")
				return 1;
		}

		return 0;

	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("CanSubscribe OUT", 0,false);
		return 0;
	}


	Log("CanSubscribe OUT", 0,false);
	return 1;
}


std::string ReadZorroIniConfig(std::string key)
{
	char bufDir[300];
	GetCurrentDirectoryA(300, bufDir);
	strcat(bufDir, "\\Zorro.ini");

	char buf[300];
	int success = GetPrivateProfileStringA("BITTREX", key.c_str(), "", buf, 300, bufDir);
	std::string ret = std::string(buf);
	if (ret == std::string(""))
	{
		BrokerError(key.c_str());
		BrokerError(" not set in Zorro.ini");
	}

	return ret;
}

////////////////////////////////////////////////////////////////

DLLFUNC int BrokerHTTP(FARPROC fp_send, FARPROC fp_status, FARPROC fp_result, FARPROC fp_free)
{
	(FARPROC&)http_send = fp_send;
	(FARPROC&)http_status = fp_status;
	(FARPROC&)http_result = fp_result;
	(FARPROC&)http_free = fp_free;
	return 1;
}

int BrokerHistory3(std::string sAsset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, T6* ticks)
{
	Log("BrokerHistory3 IN", 0, false);



	std::string fsym = "";
	std::string tsym = "";
	int len = 0;
	int pos = sAsset.find("-");
	if (pos > 0)
	{
		tsym = sAsset.substr(0, pos );
		len = tsym.length();
		fsym = sAsset.substr(pos+1, sAsset.length() - len);
	}

	
	std::string aggregate = "1";
	std::string action = "histominute";
	if (nTickMinutes == (int)MIN1 || nTickMinutes == (int)MIN15 || nTickMinutes == (int)MIN30)
	{
		action = "histominute";
		aggregate = itoa(nTickMinutes);
	}
	else if (nTickMinutes == (int)MIN60 || nTickMinutes == (int)MIN240)
	{
		action = "histohour";
		aggregate = "1";
		if (nTickMinutes == (int)MIN240)
			aggregate = "4";
	}
	else if (nTickMinutes == (int)MIN1440 || nTickMinutes != (int)MIN10080 || nTickMinutes != (int)MIN21600)
	{
		action = "histoday";
		aggregate = "1";
		if (nTickMinutes == (int)MIN10080)
			aggregate = "7";
		if (nTickMinutes == (int)MIN21600)
			aggregate = "30";
	}

	COleDateTime odtStart(tStart);
	COleDateTime odtEnd(tEnd);
	CStringA sStart = odtStart.Format(TEXT("%c"));
	CStringA sEnd = odtEnd.Format(TEXT("%c"));
	std::string toTs =itoa( convertTime(tEnd));

	std::string limit = uitoa(nTicks);
	int nTick = 0;
	T6* tick = ticks;

	try
	{
		//while (convertTime((__time32_t)(atol(toTs.c_str()))) >tStart)
		//{
			std::string ApiPath = "https://min-api.cryptocompare.com/data/" + action + "?fsym=" + fsym + "&tsym=" + tsym + "&limit=" + limit + "&aggregate=" + aggregate + "&e=CCCAGG&toTs=" + toTs;

			picojson::value::object objJson;
			if (!CallAPIAndParse(ApiPath, objJson, sAsset, "", true, false))
			{
				Log("Error calling API BrokerHistory3", 1, true);
				Log("BrokerHistory3 OUT", 0, false);
				return 0;
			}

			const picojson::value::array& objData = objJson.at("Data").get<picojson::array>();

			
			//Start from the most recent quote
			for (picojson::value::array::const_iterator iC = objData.end(); iC != objData.begin(); --iC)
			{
				if (iC == objData.end())
					continue;

				if (iC->get("close").to_str() == "0")
					continue;

				//Set time
				COleDateTime tTickTime;
				tTickTime = convertTime((__time32_t)(atol(iC->get("time").to_str().c_str())));
				toTs = iC->get("time").to_str();
				if (tTickTime.m_dt<tStart || tTickTime.m_dt>tEnd)
					continue;
				tick->time = tTickTime.m_dt;

				//Set Open
				tick->fOpen = atof(iC->get("open").to_str().c_str());
				//Set High
				tick->fHigh = atof(iC->get("high").to_str().c_str());
				//Set Low
				tick->fLow = atof(iC->get("low").to_str().c_str());
				//Set Close
				tick->fClose = atof(iC->get("close").to_str().c_str());
				//Set Volume
				tick->fVol = atof(iC->get("volumeto").to_str().c_str());

				if (!BrokerProgress(100 * nTick / nTicks))
					break;


				if (nTick == nTicks /*- 1*/)
					break;

				tick++;
				nTick++;

			}
		//}
	}
	catch (const std::exception& e)
	{
		Log(e.what(), 1, true);
		Log("BrokerHistory3 OUT", 0, false);
		return 0;
	}
	

	Log("return nTick: " + itoa(nTick), 0, false);
	Log("BrokerHistory3 OUT", 0, false);
	return nTick;
}

//DLLFUNC int BrokerHistory(char* Asset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, TICK* ticks)
DLLFUNC int BrokerHistory2(char* Asset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, T6* ticks)
{
	Log("BrokerHistory2 IN", 0,false);

	if (!IsLoggedIn())
		return 0;

	std::string sAsset = GetAsset(Asset);
	
	COleDateTime odtStart(tStart);
	COleDateTime odtEnd(tEnd);
	CStringA sStart = odtStart.Format(TEXT("%c"));
	CStringA sEnd = odtEnd.Format(TEXT("%c"));
	COleDateTimeSpan diff = odtEnd - odtStart;
	char sTickMinutes[8];
	_itoa(nTickMinutes, sTickMinutes,10);
	char sTicks[8];
	_itoa(nTicks, sTicks, 10);
	int TotalMinutes = diff.GetTotalMinutes();
	int TotalDays = diff.GetTotalDays();
	CStringA logstring = "Params " + CStringA(sAsset.c_str()) + " Start : " + sStart + " End : " + sEnd + " nTickMinutes : " + CStringA(sTickMinutes) + " nTicks : " + CStringA(sTicks);
	std::string logstring2((char*)(LPCTSTR)logstring.GetString());
	Log(logstring2, 0,false);

	if (nTickMinutes == 0)
		nTickMinutes = 1;
	if (nTickMinutes != (int)MIN1 && nTickMinutes != (int)MIN5 && nTickMinutes != (int)MIN15 && nTickMinutes != (int)MIN30 && nTickMinutes != (int)MIN60 &&  nTickMinutes != (int)MIN240  && nTickMinutes != (int)MIN1440  && nTickMinutes != (int)MIN10080  && nTickMinutes != (int)MIN21600)
	{
		Log("TimeFrame not supported", 1, true);
		return 0;
	}

	if (g_UseCryptoCompareHistory)
		return BrokerHistory3(sAsset, tStart, tEnd, nTickMinutes, nTicks, ticks);

	std::string action = "oneMin";
	if (nTickMinutes == (int)MIN1 || nTickMinutes == (int)MIN15 || nTickMinutes == (int)MIN30)
		action = "oneMin";
	else if (nTickMinutes == (int)MIN5 || nTickMinutes == (int)MIN15 || nTickMinutes == (int)MIN30)
		action = "fiveMin";
	else if (nTickMinutes == (int)MIN30)
		action = "thirtyMin";
	else if (nTickMinutes == (int)MIN60 || nTickMinutes == (int)MIN240)
		action = "hour";
	else if (nTickMinutes == (int)MIN1440 || nTickMinutes != (int)MIN10080 || nTickMinutes != (int)MIN21600)
		action = "day";

	int nTick = 0;
	T6* tick = ticks;

	try
	{
		//while (convertTime((__time32_t)(atol(toTs.c_str()))) >tStart)
		//{
		std::string ApiPath = "https://bittrex.com/Api/v2.0/pub/market/GetTicks?marketName=" + sAsset + "&tickInterval=" + action ;

		picojson::value::object objJson;
		if (!CallAPIAndParse(ApiPath, objJson, sAsset, "", false, false))
		{
			Log("Error calling API BrokerHistory2", 1, true);
			Log("BrokerHistory3 OUT", 0, false);
			return 0;
		}

		const picojson::value::array& objData = objJson.at("result").get<picojson::array>();
		//Start from the most recent quote
		for (picojson::value::array::const_iterator iC = objData.end(); iC != objData.begin(); --iC)
		{
			if (iC == objData.end())
				continue;

			if (iC->get("C").to_str() == "0")
				continue;

			//Set time
			
			std::string time = iC->get("T").to_str();
			COleDateTime tTickTime(atoi(time.substr(0, 4).c_str()), atoi(time.substr(5, 2).c_str()), atoi(time.substr(8, 2).c_str()), atoi(time.substr(11, 2).c_str()), atoi(time.substr(14, 2).c_str()), atoi(time.substr(17, 2).c_str()));
			if (tTickTime.m_dt<tStart || tTickTime.m_dt>tEnd)
				continue;
			tick->time = tTickTime.m_dt;

			//Set Open
			tick->fOpen = atof(iC->get("O").to_str().c_str());
			//Set High
			tick->fHigh = atof(iC->get("H").to_str().c_str());
			//Set Low
			tick->fLow = atof(iC->get("L").to_str().c_str());
			//Set Close
			tick->fClose = atof(iC->get("C").to_str().c_str());
			//Set Volume
			tick->fVol = atof(iC->get("V").to_str().c_str());

			if (!BrokerProgress(100 * nTick / nTicks))
				break;


			if (nTick == nTicks /*- 1*/)
				break;

			tick++;
			nTick++;

		}
		//}
	}
	catch (const std::exception& e)
	{
		Log(e.what(), 1, true);
		Log("BrokerHistory2 OUT", 0, false);
		return 0;
	}

	Log("BrokerHistory2 OUT", 0,false);
	return nTick;
}


DLLFUNC int BrokerAsset(char* Asset, double* pPrice, double* pSpread, double *pVolume, double *pPip, double *pPipCost, double *pMinAmount,
	double *pMargin, double *pRollLong, double *pRollShort)
{

	Log("BrokerAsset IN Symbol: " + std::string(Asset), 0,false);

	if (!IsLoggedIn())
		return 0;

	std::string sAsset = GetAsset(Asset);
	std::string logDetails ="Asset Details: " + sAsset + " ";

	//Subscribe the asset
	if (!pPrice)
		return CanSubscribe(sAsset);
	else
		*pPrice = 0.;

	if (pVolume)
		*pVolume = 0.;

	int ret = 0;
	std::string ApiPath = "public/getmarketsummary?market=" + sAsset;

	try
	{
		AssetTicker at;
		at.ask = 0; at.bid = 0; at.minAmount = 0; at.spread = 0; at.volume = 0;
		AssetTicker *SymbolLastPriceVal = SymbolLastPrice.TryGetValue(sAsset);

		picojson::value::object objJson;
		if (!CallAPIAndParse(ApiPath, objJson, sAsset, "", false))
		{
			Log("Error calling API BrokerAsset", 1,true);
			Log("BrokerAsset OUT", 0,false);
			return 0;
		}

		picojson::array resultArr = objJson.at("result").get<picojson::array>();
		if (pPrice)
		{
			*pPrice = atof(resultArr.begin()->get("Ask").to_str().c_str());
			at.ask = *pPrice;
			logDetails = logDetails + "Price: " + ftoa(*pPrice) + " ";
		}


		if (pSpread && pPrice)
		{
			*pSpread = *pPrice - atof(resultArr.begin()->get("Bid").to_str().c_str());
			at.bid = atof(resultArr.begin()->get("Bid").to_str().c_str());
			at.spread = *pSpread;
			logDetails = logDetails + "Spread: " + ftoa(*pSpread) + " ";
		}


		if (pVolume)
		{
			*pVolume = atof(resultArr.begin()->get("BaseVolume").to_str().c_str())/1440;
			at.volume = *pVolume;
			logDetails = logDetails + "Volume: " + ftoa(*pVolume) + " ";
		}




		////////////////  Parse the Assetpairs json  ////////////////////////
		picojson::value::object::const_iterator iA = g_AssetPairs.begin(); ++iA;
		const picojson::value::array& objData = iA->second.get<picojson::array>();
		for (picojson::value::array::const_iterator iC = objData.begin(); iC != objData.end(); ++iC)
		{

			/*Optional output, size of 1 PIP, f.i. 0.0001 for EUR/USD.
			Size of 1 pip in counter currency units; accessible with the PIP variable.About ~1 / 10000 of the asset price.
			The pip size is normally 0.0001 for assets(such as currency pairs) with a single digit price, 0.01 for assets with a price between 10 and 200
			(such as USD / JPY and most stocks), and 1 for assets with a 4 - or 5 - digit price.For consistency, use the same pip sizes for all your asset lists.
			*/
			if (pPip)
			{
				//const picojson::value& pair_decimals = ????
				//char  decval[] = "0.0000000000";
				//int ipair_decimals = atoi(pair_decimals.to_str().c_str());
				//decval[ipair_decimals + 1] = '1';
				//*pPip = atof(decval);
				//logDetails = logDetails + "Pip: " + ftoa(*pPip) + " ";
				if (g_BaseTradeCurrency=="USDT")
					*pPip = 0.01;
				else
					*pPip = 0.00000001;
			}


			/*Optional output, minimum order size, i.e.number of contracts for 1 lot of the asset.For currencies it's usually 10000 with mini lot accounts and
			1000 with micro lot accounts. For CFDs it's usually 1, but can also be a fraction of a contract, f.i. 0.1.
			https://support.kraken.com/hc/en-us/articles/205893708-What-is-the-minimum-order-size-
			Number of contracts for 1 lot of the asset; accessible with the LotAmount variable.
			It's the smallest amount that you can buy or sell without getting the order rejected or a "odd lot size" warning.
			For currencies the lot size is normally 1000 on a micro lot account, 10000 on a mini lot account, and 100000 on standard lot accounts.
			Some CFDs can have a lot size less than one contract, such as 0.1 contracts. For most other assets it's normally 1 contract per lot.
			*/

			if (pMinAmount)
			{
				*pMinAmount = (atoi(g_MinOrderSize.c_str()) * *pPip) / (*pPrice-*pSpread);
				at.minAmount = *pMinAmount;

				if (SymbolLastPriceVal)
					logDetails = logDetails + "MinAmount: " + ftoa(SymbolLastPriceVal->minAmount) + " ";
				else
					logDetails = logDetails + "MinAmount: " + ftoa(*pMinAmount) + " ";
			}


			/*  Optional output, cost of 1 PIP profit or loss per lot, in units of the account currency.
			Value of 1 pip profit or loss per lot, in units of the account currency. Accessible with the PipCost variable and internally used for calculating the trade profit.
			When the asset price rises or falls by x, the equivalent profit or loss of a trade in account currency is x * Lots * PIPCost / PIP. For assets with pip size 1 and
			one contract per lot, the pip cost is just the conversion factor from counter currency to account currency. For calculating it manually, multiply LotAmount with
			PIP and divide by the price of the account currency in the asset's counter currency. Example 1: AUD/USD on a micro lot EUR account has PipCost
			of 1000 * 0.0001 / 1.11 (current EUR/USD price) = 0.09 EUR. Example 2: AAPL stock on a USD account has PipCost of 1 * 0.01 / 1.0 = 0.01 USD = 1 cent.
			Example 3: S&P500 E-Mini futures on a USD account have PipCost of 50 USD (1 point price change of the underlying is equivalent to $50 profit/loss of an
			S&P500 E-Mini contract).

			if account curr==basecurrency   1 * *pPip / 1
			if account curr==ZUSD   1 * *pPip / XBT/USD
			*/
			if (pPipCost)
			{
				*pPipCost = (*pMinAmount * *pPip) * g_BaseCurrAccCurrConvRate;
				logDetails = logDetails + "PipCost: " + ftoa(*pPipCost) + " ";
			}

			/*  pMarginCost
			Optional output, initial margin cost for buying 1 lot of the asset in units of the account currency. Alternatively, the leverage of the asset when
			negative (f.i. -50 for 50:1 leverage). If not supported, calculate it as decribed under asset list.

			Initial margin for purchasing 1 lot of the asset in units of the account currency. Depends on account leverage, account currency, and counter currency;
			accessible with the MarginCost variable. Internally used for the conversion from trade Margin to Lot amount: the number of lots that can be purchased with
			a given trade margin is Margin / MarginCost. Also affects the Required Capital and the Annual Return in the performance report.
			Can be left at 0 when Leverage (see below) is used for determining the margin.

			MarginCost = Asset price / Leverage * PipCost / PIP

			*/
			//if (pMargin)
			//	*pMargin = 0;

			Log(logDetails, 0, false);

			break;
		}

		if (!SymbolLastPriceVal)
			SymbolLastPrice.Add(sAsset, at);
		else
		{
			if (at.ask != 0) SymbolLastPriceVal->ask = at.ask;
			if (at.bid != 0) SymbolLastPriceVal->bid = at.bid;
			//we set the MinAmount at the beginning only, otherwise it will change depending on the rate
			//if (at.minAmount != 0) SymbolLastPriceVal->minAmount = at.minAmount;
			if (at.spread != 0) SymbolLastPriceVal->spread = at.spread;
			if (at.volume != 0) SymbolLastPriceVal->volume = at.volume;
		}

	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("BrokerAsset OUT", 0,false);
		return 0;
	}




	//return 1 if successfull
	Log("BrokerAsset OUT", 0,false);
	return 1;

}

/////////////////////////////////////////////////////////////////////
DLLFUNC int BrokerTime(DATE *pTimeGMT)
{

	//TODO remove
	//return 2;

	Log("BrokerTime IN", 0,false);

	int ret = 0;
	std::string ApiPath = "";

	try
	{



	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("BrokerTime OUT", 0,false);
		return 0;
	}

	/*
	{"error":[],"result":{"unixtime":1497422201,"rfc1123":"Wed, 14 Jun 17 06:36:41 +0000"}}

	0 when the connection to the server was lost, and a new login is required.
	1 when the connection is ok, but the market is closed or trade orders are not accepted.
	2 when the connection is ok and the market is open for trading at least one of the subscribed assets.
	*/
	Log("BrokerTime OUT", 0,false);
	return 2;
}


/*
Optional function. Returns the current account status, or changes the account if multiple accounts are supported. Called repeatedly during the trading session. 
If the BrokerAccount function is not provided, f.i. when using a FIX API, Zorro calculates balance, equity, and total margin itself.
Parameters:
Account Input, new account number or NULL for using the current account.
pBalance Optional output, current balance on the account.
pTradeVal Optional output, current value of all open trades; the difference between account equity and balance. 
If not available, it can be replaced by a Zorro estimate with the SET_PATCH broker command.
pMarginVal Optional output, current total margin bound by all open trades.
*/

DLLFUNC int BrokerAccount(char* Account, double *pdBalance, double *pdTradeVal, double *pdMarginVal)
{
	Log("BrokerAccount IN", 0,false);

	
	//std::string ApiPath = "account/getbalances";
	//std::string data = "";
	std::string ApiPath = "account/getbalance";
	std::string data = "currency=" + g_BaseTradeCurrency;

	try
	{

		picojson::value::object objJson;
		if (!CallAPIAndParse(ApiPath, objJson, "Account", data, true,true,true))
		{
			Log("Error calling API BrokerAccount", 1,true);
			Log("BrokerAccount OUT", 0, false);
			return 0;
		}

		if (pdBalance)
		{
			*pdBalance = atof(objJson.at("result").get("Available").to_str().c_str());
			if (CallGetAccBaseCurrExchRate() == 0)
			{
				Log("BrokerAccount OUT", 0, false);
				return 0;
			}
			*pdBalance = *pdBalance * g_BaseCurrAccCurrConvRate;
		}
		

	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("BrokerAccount OUT", 0,false);
		return 0;
	}

	Log("BrokerAccount OUT", 0,false);
	return 1;
}


/*
Enters a long or short trade at market. Also used for NFA compliant accounts to close a trade by opening a position in the opposite direction.
Asset Input, name of the asset, f.i. "EUR/USD". Some broker APIs don't accept a "/" slash in an asset name; the plugin must remove the slash in that case.
nAmount Input, number of contracts, positive for a long trade and negative for a short trade. The number of contracts is the number of lots multiplied with the LotAmount.
If LotAmount is < 1 (f.i. for a CFD with 0.1 contracts lot size), the number of lots is given here instead of the number of contracts.
dStopDist Input, 'safety net' stop loss distance to the opening price, or 0 for no stop. This is not the real stop loss, which is handled by the trade engine. 
Placing the stop is not mandatory. NFA compliant orders do not support a stop loss; 
in that case dStopDist is 0 for opening a trade and -1 for closing a trade by opening a position in opposite direction.
pPrice Optional output, the current asset price at which the trade was opened.
Returns:
Trade ID number when opening a trade, or 1 when buying in opposite direction for closing a trade the NFA compliant way, or 0 when the trade could not be opened or closed. 
If the broker API does not deliver a trade ID number (for instance with NFA brokers that do not store individual trades), 
the plugin can just return an arbitrary unique number f.i. from a counter.
*/
DLLFUNC int BrokerBuy(char* Asset, int nAmount, double dStopDist, double *pPrice)
{
	Log("BrokerBuy IN", 0,false);
	int ret = 0;

	if (!IsLoggedIn())
		return 0;

	std::string sAsset = GetAsset(Asset);
	Log("BrokerBuy Params: " + sAsset + " Amount:" + itoa(nAmount) + " StopDist:" + ftoa(dStopDist),0,false);

	
	if (nAmount == 0) return 0;
	std::string orderType;
	if (dStopDist == 0)
		orderType = nAmount > 0 ? "buylimit" : "selllimit";
	else if (dStopDist == -1)
		orderType = nAmount < 0 ? "selllimit" : "buylimit";
	else
	{
		Log("Error calling API BrokerBuy - Type undefined NFA Flag set ?", 1, true);
		Log("BrokerBuy OUT", 0, false);
		return 0;
	}
	nAmount = abs(nAmount);

	// price  without orderbook
	AssetTicker *lastPrice = SymbolLastPrice.TryGetValue(sAsset);
	std::string sRate = "";
	if (lastPrice)
	{
		if (orderType=="buylimit")
			sRate = ftoa(lastPrice->ask);
		else if (orderType == "selllimit")
			sRate = ftoa(lastPrice->bid);
	}
	else
	{
		Log("Error calling API BrokerBuy - Last Price undefined", 1, true);
		Log("BrokerBuy OUT", 0, false);
		return 0;
	}
	


	double famount = nAmount;
	int iamount = nAmount;
	std::string sAmount = "";
	double minOrderSize = lastPrice->minAmount;
	if (minOrderSize == 0)
		return 0;
	if (minOrderSize < 1)
	{
		famount = nAmount* minOrderSize;
		sAmount = ftoa(famount);
	}
	else
		sAmount = itoa(nAmount);

	std::string tradeId = GetTradeIDstring();

	try
	{
		picojson::value::object objJson;

		//Get Order Book
		std::string type = orderType.substr(0, orderType.find("limit"));
		if (type == "buy")type = "sell";
		else if (type == "sell")type = "buy";
		std::string data = "market=" + sAsset + "&type=" + type;
		std::string ApiPath = "public/getorderbook?" + data;

		if (!CallAPIAndParse(ApiPath, objJson, sAsset, data, false, true, false))
		{
			Log("Error calling API getorderbook", 1, true);
			Log("BrokerBuy OUT", 0, false);
			return 0;
		}

		const picojson::value::array& objData = objJson.at("result").get<picojson::array>();
		double sumQuantity = 0;
		int count = 0;
		for (picojson::value::array::const_iterator iC = objData.begin(); iC != objData.end(); ++iC)
		{
			count++;
			double Quantity = atof( iC->get("Quantity").to_str().c_str());
			double Rate = atof(iC->get("Rate").to_str().c_str());
			sumQuantity = sumQuantity + Quantity;
			if (famount < sumQuantity || count >= g_LimitOrderMaxOrderBookDepth)
			{
				sRate = ftoa(Rate);
				if (pPrice)
					*pPrice = Rate;
				break;
			}
		}

		//Buy/Sell
		objJson.clear();
		data = "market=" + sAsset + "&quantity=" + sAmount + "&rate=" + sRate;
		ApiPath = "market/" + orderType;
		Log(data, 0, false);
		
		if (g_DemoTrading)
		{
			std::string content = "{\"success\" : true,\"message\" : \"\",\"result\" : {\"uuid\" : \"54fe52f4-09e4-4db9-95a4-af249c879133\"}}";
			Parse(ApiPath, objJson, content, sAsset, data);
			/*double *jsonCached = DemoTradesID.TryGetValue(atoi(tradeId.c_str()));
			if (!jsonCached)
				DemoTradesID.Add(atoi(tradeId.c_str()), famount);*/

		}
		else
		{
			if (!CallAPIAndParse(ApiPath, objJson, sAsset, data,false,true,true))
			{
				Log("Error calling API BrokerBuy", 1, true);
				Log("BrokerBuy OUT", 0, false);
				return 0;
			}
		}
		std::string uuid = objJson.at("result").get("uuid").to_str();

		//check trade result
		if (g_WaitAfterOrderForCheckSec>0)
			Sleep(g_WaitAfterOrderForCheckSec*1000);//wait trade to execute and get a response
		if (uuid.length() > 8 || g_DemoTrading)
		{
			objJson.clear();
			orderType = orderType.substr(0,orderType.find("limit"));
			std::string data = "uuid=" + uuid;
			ApiPath = "account/getorder";
			if (!CallAPIAndParse(ApiPath, objJson, sAsset, data, false, true, true))
			{
				Log("Error calling API BrokerBuy", 1, true);
				Log("BrokerBuy OUT", 0, false);
				return 0;
			}

			std::string QuantityRemaining = "0";
			std::string Quantity = "0";
			std::string CommissionPaid = "";
			if (objJson.at("result").get<picojson::object>().count("QuantityRemaining") > 0)
			{
				QuantityRemaining = objJson.at("result").get("QuantityRemaining").to_str();
				Quantity = objJson.at("result").get("Quantity").to_str();
				//This is calculated by taking the (amount * purchase price * .0025). 
				double commission = atof(sAmount.c_str()) * atof(sRate.c_str()) * 0.0025;
				CommissionPaid = ftoa(commission);// objJson.at("result").get("CommissionPaid").to_str();
			}


			Log("Trade id: " + uuid + " - " + tradeId + " - Price: " + sRate, 0, true);
			Log("Type: " + orderType + " Price: " + sRate + " Amount: " + sAmount, 0, true);
			Log("Quantity: " + Quantity + " QRemaining: " + QuantityRemaining + " Commission: " + CommissionPaid, 0, true);

		}

		if (uuid == "null" && !g_DemoTrading)
		{
			Log("Error calling API BrokerBuy", 1, true);
			Log("BrokerBuy OUT", 0, false);
			return 0;
		}


	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("BrokerBuy OUT", 0,false);
		return 0;
	}


	Log("BrokerBuy OUT", 0,false);
	if (dStopDist == -1)
		return 1;
	else
		return atoi(tradeId.c_str());

}

// returns negative amount when the trade was closed
DLLFUNC int BrokerTrade(int nTradeID, double *pOpen, double *pClose, double *pRoll, double *pProfit)
{
	Log("BrokerTrade IN", 0,false);

	if (!IsLoggedIn())
		return 0;

	//TODO
	//nTradeID = 1502429247;
	if (g_DemoTrading)
	{
		//double *DemoTrade = DemoTradesID.TryGetValue(nTradeID);
		return 0;//*DemoTrade;
	}

	int ret = 0;
	std::string ApiPath = "/0/private/ClosedOrders";//QueryOrders"

	char cTradeID[20];
	_itoa(nTradeID, cTradeID, 10);
	std::string sTradeID(cTradeID);
	std::string data = "trades=true&userref=" + sTradeID;
	std::string logDetails = "TradeID Details: " + sTradeID + " ";

	try
	{
		//TODO
		//std::string content;// = "{\"error\":[],\"result\":{\"closed\":{\"OQLOGW-HS7AR-PX3HJR\":{\"refid\":null,\"userref\":1502429247,\"status\":\"closed\",\"reason\":\"User canceled\",\"opentm\":1502421611.0928,\"closetm\":1502421767.3221,\"starttm\":0,\"expiretm\":0,\"descr\":{\"pair\":\"XDGXBT\",\"type\":\"buy\",\"ordertype\":\"limit\",\"price\":\"0.00000010\",\"price2\":\"0\",\"leverage\":\"none\",\"order\":\"buy 3000.00000000 XDGXBT @ limit 0.00000010\"},\"vol\":\"3000.00000000\",\"vol_exec\":\"3000.00000000\",\"cost\":\"0.00168000\",\"fee\":\"0.00000436\",\"price\":\"0.00000056\",\"misc\":\"\",\"oflags\":\"fciq\"}},\"count\":1}}";
		Log(data, 0,false);
		picojson::value::object objJson;
		if (!CallAPIAndParse(ApiPath, objJson, "", data, true))
		{
			Log("Error calling API BrokerTrade", 1,true);
			Log("BrokerTrade OUT", 0,false);
			return 0;
		}

		picojson::value::object::const_iterator iA = objJson.begin(); ++iA;
		Log(iA->second.serialize(), 0,false);
		const picojson::value::object& firstBlock = iA->second.get<picojson::object>().begin()->second.get<picojson::object>();
		const picojson::value::object& descrBlock = firstBlock.begin()->second.get("descr").get<picojson::object>();
		std::string pair = descrBlock.at("pair").to_str();
		logDetails = logDetails + "Pair: " + pair + " ";

		if (pOpen)
		{
			*pOpen = atof(firstBlock.begin()->second.get("price").to_str().c_str());
			logDetails = logDetails + "Open: " + ftoa(*pOpen) + " ";
		}

		std::string type = descrBlock.at("type").to_str();
		logDetails = logDetails + "Type: " + type + " ";

		AssetTicker *lastPrice = SymbolLastPrice.TryGetValue(pair);
		if (pClose && lastPrice)
		{
			if (type=="buy")
				*pClose = lastPrice->ask;
			else if (type == "sell")
				*pClose = lastPrice->bid;
			logDetails = logDetails + "Close: " + ftoa(*pClose) + " ";
		}

		double execAmount = atof(firstBlock.begin()->second.get("vol_exec").to_str().c_str());
		ret = execAmount / lastPrice->minAmount;
		logDetails = logDetails + "Vol: " + ftoa(execAmount) + " ";

		double fee = atof(firstBlock.begin()->second.get("fee").to_str().c_str());
		logDetails = logDetails + "Fee: " + ftoa(fee) + " ";

		if (pProfit)
		{
			if (type=="buy")
				*pProfit = (*pClose * execAmount) - (*pOpen * execAmount) - fee;
			else if (type=="sell")
				*pProfit = (*pOpen * execAmount) - (*pClose * execAmount) - fee;

			logDetails = logDetails + "Profit " + g_BaseTradeCurrency + ": " + ftoa(*pProfit) + " ";
			if (g_BaseTradeCurrency!=g_DisplayCurrency)
				logDetails = logDetails + "Profit " + g_DisplayCurrency + ": " + ftoa(*pProfit * g_BaseCurrAccCurrConvRate) + " ";
		}

		*pRoll = 0;
		
		Log(logDetails, 0, true);

	}
	catch (const std::exception& e)
	{
		Log(e.what(),1,true);
		Log("BrokerTrade OUT", 0,false);
		return 0;
	}


	Log("BrokerTrade OUT", 0, false);
	return ret;
}

//DLLFUNC int BrokerSell(int nTradeID, int nAmount)
//{
//	Log("BrokerSell IN", 0);
//	if (!IsLoggedIn())
//		return 0;
//
//	int ret = 0;
//
//	Log("BrokerSell OUT", 0);
//	return ret;
//}

// 0 = test, 1 = relogin, 2 = login, -1 = logout
DLLFUNC int BrokerLogin(char* User, char* Pwd, char* Type, char* Account)
{
	Log("BrokerLogin IN", 0, false);



#ifdef FAKELOGIN
	return 1;
#endif

	if (User == NULL)
	{
		g_nLoggedIn = 0;
		ClearCache();
		return 0;
	}


	if (g_nLoggedIn == 0)
	{

		g_PrivateKey = ReadZorroIniConfig(std::string("PrivateKey"));
		g_PublicKey = ReadZorroIniConfig(std::string("PublicKey"));
		g_IntervalSec = atoi(ReadZorroIniConfig(std::string("IntervalSec")).c_str());
		g_BaseTradeCurrency = ReadZorroIniConfig(std::string("BaseTradeCurrency"));
		g_DisplayCurrency = ReadZorroIniConfig(std::string("DisplayCurrency"));
		g_DemoTrading = atoi(ReadZorroIniConfig(std::string("DemoTrading")).c_str());
		g_EnableLog = atoi(ReadZorroIniConfig(std::string("EnableLog")).c_str());
		g_UseCryptoCompareHistory = atoi(ReadZorroIniConfig(std::string("UseCryptoCompareHistory")).c_str());
		g_MinOrderSize = ReadZorroIniConfig(std::string("MinOrderSize"));
		g_LimitOrderMaxOrderBookDepth = atoi(ReadZorroIniConfig(std::string("LimitOrdermaxorderBookDepth")).c_str());
		g_WaitAfterOrderForCheckSec = atoi(ReadZorroIniConfig(std::string("WaitAfterOrderForCheckSec")).c_str());

		if (g_DisplayCurrency == "USD")
			g_DisplayCurrency = "USDT";
		if (g_BaseTradeCurrency == "USD")
			g_BaseTradeCurrency = "USDT";


		if (CallGetAccBaseCurrExchRate() == 0)
		{
				Log("BrokerLogin OUT", 0, false);
				return 0;
		}


		double val1 = 0;
		double val2 = 0;
		double val3 = 0;
		char account[20];
		ClearCache();
		g_nLoggedIn = BrokerAccount(account, &val1, &val2, &val3);
		if (g_nLoggedIn == 0)
		{
			Log("Can't Login - Retry",1 ,true);
			Log("BrokerLogin OUT", 0, false);
		}
		if (g_DemoTrading)
			Log("DEMO mode.",0,true);



		//////////////////////TODO

		//double p1=0;
		//double p2=0;
		//double p3=0;
		//double p4=0;
		//BrokerTrade(1502429247, &p1, &p2, &p3, &p4);   //Order OQLOGW-HS7AR-PX3HJR
		///////////////////////
	}


	Log("BrokerLogin OUT", 0, false);
	return g_nLoggedIn;
}

DLLFUNC int BrokerOpen(char* Name, FARPROC fpError, FARPROC fpProgress)
{
	Log("BrokerOpen IN", 0, false);


	int ret = 2;

	(FARPROC&)BrokerError = fpError;
	(FARPROC&)BrokerProgress = fpProgress;

	strcpy(Name, "BitTrex");

	Log("BrokerOpen OUT", 0, false);
	return ret;

}

//DLLFUNC int BrokerStop(int nTradeID, double dStop)
//{
//	Log("BrokerStop IN", 0,false);
//
//	if (!IsLoggedIn())
//		return 0;
//
//	int ret = 0;
//
//
//	Log("BrokerStop OUT", 0,false);
//	return ret;
//}

DLLFUNC double BrokerCommand(int nCommand, DWORD dwParameter)
{
	Log("BrokerCommand IN", 0, false);

	if (!IsLoggedIn())
		return 0;


	int ret = 0;

	Log("BrokerCommand OUT", 0, false);
	return ret;
}