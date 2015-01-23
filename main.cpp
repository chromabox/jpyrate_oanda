//
// OANDAのStream APIを利用してJPY レート取得するサンプル
//
// The MIT License (MIT)
//
// Copyright (c) <2015> chromabox <chromarockjp@gmail.com>
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <getopt.h>
#include <unistd.h>

#include "http/httpcurl.hpp"
#include "include/picojson.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

using namespace std;

// バージョン
static const string THIS_VERSION	= "0.0.1";

// デモアカウント用としておく
#define USE_DEMO

// レート取得用URL。
#ifdef USE_DEMO
static const std::string	REST_URL	= "https://api-fxpractice.oanda.com/v1/prices";
static const std::string	STREAM_URL	= "https://stream-fxpractice.oanda.com/v1/prices";
#else
static const std::string	REST_URL	= "https://api-fxtrade.oanda.com/v1/prices";
static const std::string	STREAM_URL	= "https://stream-fxtrade.oanda.com/v1/prices";
#endif

// ユーザアクセストークン。いちいち聞かれるのが面倒なときはここに記述してコンパイルするべし
static const std::string	USER_ACCESS_TOKEN	= "";
// ユーザID（口座番号）
static const std::string	USER_ID	= "";



// OANDA APIでは標準時刻(UTC)が記述されている。しかもUNIX系式ではなく
// 10桁より多く返し、前10桁がUNIX時間、後ろ7桁がミリ秒らしい。
// ミリ秒以下は今のところいらないので、そこは変換していない。
void get_local_time_string(const std::string &src,std::string &dst)
{
	string tmstr;
	time_t tm;
	struct tm tm_dest;
	char tmpstr[64];
	
	memset(&tm_dest,0,sizeof(struct tm));
	// 10桁まで有効。それ以降はミリ秒と思われる。。。。
	tmstr = src.substr(0,10);
	// 文字形式のをまずは変換
	tm = strtoull(tmstr.c_str(),NULL,10);
	// 現地時間に直す
	localtime_r(&tm,&tm_dest);
	
	strftime(tmpstr,sizeof(tmpstr),"[%Y-%m-%d %T]",&tm_dest);
	dst = tmpstr;
}

// RESTAPIだけで取ってくる場合の参考用
#if 0
// JSONの解析をする
bool parseJson(const string &src,picojson::array &jarray)
{
	picojson::value jsonval;	
	picojson::object jobj;	
	string json_err;
	
	picojson::parse(jsonval,src.begin(),src.end(),&json_err);
	if(!json_err.empty()){
		cout << "[JSON] parse err!!! " << endl;
		cout << json_err << endl;
		cout << src << endl;
		return false;
	}
	if(!jsonval.is<picojson::object>()){
		cout << "[JSON] is not object... " << endl;
		return false;
	}
	jobj = jsonval.get<picojson::object>();
	
	if(! jobj["prices"].is<picojson::array>()){
		cout << "受信データのエラーです。終了します" << endl;
		return false;
	}
	jarray = jobj["prices"].get<picojson::array>();
	return true;
}

// USD/JPYの為替レートを取得
bool readRate(const string &atoken,picojson::array &jarray)
{
	HTTPCurl peer;
	HTTPRequestData	httpdata;
	string header;
	
	// ヘッダを書く
	header = "Authorization: Bearer ";
	header += atoken;
	peer.appendHeader(header);
	// これを設定すると時間単位がUNIX系になる
	peer.appendHeader("X-Accept-Datetime-Format: UNIX");	
	
	// GETリクエストで渡すデータを指定
	httpdata["instruments"] = "USD_JPY";
	
	if(!peer.getRequest(REST_URL,httpdata)){
		cout << "request error" << endl;
		return false;
	}
	unsigned long httpcode = peer.getLastResponceCode();
	if(httpcode != 200){
		printf("HTTP code Error %lu\n",httpcode);
		return false;
	}
	// JSONでやってくるので解析
	string responce = peer.getResponceString();
	
	if(! parseJson(responce,jarray)){
		return false;
	}
	return true;
}
#endif

// JSONの解析を実際に行う(Stream API向け)
bool parseJsonStreams(const std::string src,picojson::object &jobj)
{
	picojson::value jsonval;
	string json_err;
	
	picojson::parse(jsonval,src.begin(),src.end(),&json_err);
	if(!json_err.empty()){
		cout << "[JSON] parse err!!! " << json_err << endl;
		return false;
	}
	
	// ここまできたらたぶんJSON解析成功なのでJSONのみを出す	
	if(!jsonval.is<picojson::object>()){
		cout << "[JSON] is not object... " << src << endl;
		return false;
	}
	jobj = jsonval.get<picojson::object>();
	return true;
}

// レート表示を行う
bool printRateStream(picojson::object &jobj)
{
	if(jobj["disconnect"].is<picojson::object>()){
		// disconnectがきた。。
		cout << "disconnect message. exit..." << endl;
		return false;
	}	

	// HeartBeatなどは無視。tickオブジェクトだけを見る
	if(! jobj["tick"].is<picojson::object>()) return true;
	
	picojson::object rate = jobj["tick"].get<picojson::object>();
	
	string tmstr;
	get_local_time_string(rate["time"].to_str(),tmstr);

	double bid = rate["bid"].get<double>();
	double ask = rate["ask"].get<double>();	
	double spread = (ask - bid) * 100;				// スプレッドはPips単位
	
	printf("%s bid: %.3f ask: %.3f sp: %.1f\n",
		tmstr.c_str(),
		bid,
		ask,
		spread
	);
	return true;
}


string g_bufSteam;

// perfomeから呼ばれるStramingAPI用コールバック関数。この中で受信結果を一時バッファにいれ、JSON値を引き渡す
size_t callbk_stream_internal_entry(char* ptr,size_t size,size_t nmemb,void* userdata)
{
	size_t wsize = size * nmemb;
	size_t oldsize = g_bufSteam.size();
	
	g_bufSteam.resize(g_bufSteam.size() + wsize);
	memcpy(&g_bufSteam[oldsize],ptr,wsize);
	
	// JSON要素は今のところ 0x0d 0x0a で区切られている。
	// レートが動くとまとめて来ることがあるので
	// 0x0d 0x0aを探して、それごとにJSON解析を通す
	size_t found=0;	
	found = g_bufSteam.find("\r\n");
	while(found != string::npos){
		string streams = g_bufSteam.substr(0,found);
//		cout << streams << endl;
		g_bufSteam.erase(0,found+2);
		found = g_bufSteam.find("\r\n");
		// 空の場合（よく送られてくる）は何もしない
		if(streams.empty()) continue;

		picojson::object jobj;
		if(! parseJsonStreams(streams,jobj)){
			return 0;
		}
		if(! printRateStream(jobj)) return 0;
	}
	return wsize;
}

bool execStreamRate(const string &atoken,const string &userid)
{
	HTTPCurl peer;
	HTTPRequestData	httpdata;
	string header;
	
	// ヘッダを書く
	header = "Authorization: Bearer ";
	header += atoken;
	peer.appendHeader(header);
	// これを設定すると時間単位がUNIX系になる
	peer.appendHeader("X-Accept-Datetime-Format: UNIX");	
	
	// GETリクエストで渡すデータを指定
	httpdata["accountId"] = userid;
	httpdata["instruments"] = "USD_JPY";
	
	if(!peer.getRequest(STREAM_URL,httpdata,callbk_stream_internal_entry,NULL)){
		cout << "request error" << endl;
		return false;
	}
	// ここにくるときは切断したときとかかもしれない。
	unsigned long httpcode = peer.getLastResponceCode();
	if(httpcode != 200){
		printf("HTTP code Error %lu\n",httpcode);
		return false;
	}
	return true;
}


int main(int argc,char *argv[])
{	
	tzset();
	
	string atoken = USER_ACCESS_TOKEN;
	string userid = USER_ID;
	// ユーザアクセストークンが必要なので入力を促す
	if(atoken.empty()){
		cout << "REST APIのPersonal AccessTokenの値を入力してください" << endl;
		cin >> atoken;
	}
	if(userid.empty()){
		cout << "口座IDを入力してください" << endl;
		cin >> userid;
	}
	
	if(! execStreamRate(atoken,userid)){
		return 0;
	}
	
	// RESTAPIだけで取ってくるときの参考用
#if 0
	picojson::object obj;
	picojson::object rate;
	picojson::array prices;
	if(! readRate(atoken,prices)){
		return 0;
	}
	picojson::array::iterator it;
	
	for(it=prices.begin();it!=prices.end();it++){
		if(! it->is<picojson::object>()) continue;
		picojson::object rate = it->get<picojson::object>();
		// レート表示。ask=買い bid=売り
		cout << "bid:" << rate["bid"].to_str() << " ask:" << rate["ask"].to_str() << endl;
	}
#endif
	return 0;
}


