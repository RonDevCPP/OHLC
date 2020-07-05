#include <iostream>
#include <stdlib.h>
#include <thread>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>

using namespace std;

/**
*Macro for bar interval 
*/
#define BAR_INTERVAL 15 

/**
*Mutex variable to synchronize reading and processing thread, processing and publishing thread.
*/
mutex mxJsonReader,mxPublisher;

/**
*Condition variable to singal between reading, processing and publishing thread.
*/
condition_variable cvJsonReader,cvPublisher;

bool readFinished = false;
bool processFinished = false;

/**
*structure to hold trade details
*/
struct trade
{
	string sym;
  	double price;
   	double qty;
   	long tmstmp;
};

/**
*structure to hold a trade bar details.
*
*/
struct barOHLC{
	long start_tm;
	double qty;
	double ltp;
	double open;
	double high;
	double low;
	double close;
	int bar_num;
};

/**
* Queue to transmit message between reader and processor.
*/
queue<struct trade> tradeQ;
/**
* Queue to transmit processed message to publisher.
*/
queue<string> pubQ;

/**
* Funcion to read trades of JSON format from a file.
*
* @param[in] filename string container of reference type to hold filename value.
* @return void.
*/
void reader(const string &filename)
{
   	fstream tfile;
   	tfile.open(filename, ios::in);
	
   	string line;
   	while(getline(tfile, line))
   	{
		size_t pos = line.find('{');
		if(pos != string::npos)
		{
			line.erase(pos, 1);
		}
		pos=string::npos;
		pos = line.find('}');
		if(pos != string::npos)
		{
			line.erase(pos, 1);
		}
		pos = string::npos;

		stringstream str(line);
		string tmp;
		trade tmpTrade;
		while(getline(str, tmp, ','))
		{
			size_t delPos = tmp.find(':');
			if(delPos != string::npos)
			{
				string key = tmp.substr(0,delPos);
				string value = tmp.substr(delPos+1);
				size_t wspos = key.find_first_not_of(" ");
				if(wspos != string::npos)
				{
					key.erase(0, wspos);
				}
				if(key == "\"sym\"")
				{
					tmpTrade.sym = value;
				}
				else if(key == "\"P\"")
				{
					tmpTrade.price = stod(value); 
				}
				else if(key == "\"Q\"")
				{
					tmpTrade.qty = stod(value);
				}
				else if(key == "\"TS2\"")
				{
					tmpTrade.tmstmp = stol(value)/(1000000000);
				}
			}
		}
		{
			lock_guard<mutex> lock(mxJsonReader);
			tradeQ.push(tmpTrade);
			cvJsonReader.notify_all();
		}
   	}
   	readFinished = true;
	tfile.close();
}

/**
*Function to create bar event message to be consumed by publisher. It computes and constructs a bar message .
*
*@param[in|out]  map    unordered map having symbol as key and OHLC bar as value.passed by reference to update the bar OHLC data as received.
*@param[in]      trade  structure trade contains trade details creted by reader function.	
*@return         string String value of event message with respect to updated OHLC bar details. 
*/
string createBar(unordered_map<string, barOHLC> &ohlcBar, trade trd)
{
	auto itm = ohlcBar.find(trd.sym);
	stringstream evtMsg;
	if(itm == ohlcBar.end())
	{
		 barOHLC bar{trd.tmstmp,trd.qty,trd.price,trd.price,trd.price,trd.price,0.0,1};
		 ohlcBar[trd.sym]=bar;
		 evtMsg<<"{\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
		 evtMsg<<"{\"o\":"<<bar.open<<",\"h\":"<<bar.high<<",\"l\":"<<bar.low<<",\"c\":"<<bar.close<<",\"volume\":"<<bar.qty<<",\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
    }
	else
    	{
 		barOHLC &bar = itm->second;
		long diff = (trd.tmstmp-bar.start_tm)/BAR_INTERVAL;
		 
		if(diff == 0)
		{
			bar.ltp = trd.price;
			if( bar.high < trd.price)
			{
				bar.high = trd.price;
			}
			else if(bar.low > trd.price)
			{
				bar.low = trd.price;
			}
			bar.qty += trd.qty;
			evtMsg<<"{\"o\":"<<bar.open<<",\"h\":"<<bar.high<<",\"l\":"<<bar.low<<",\"c\":"<<bar.close<<",\"volume\":"<<bar.qty<<",\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
	 	}
		else
		{
			bar.close = bar.ltp;
			evtMsg<<"{\"o\":"<<bar.open<<",\"h\":"<<bar.high<<",\"l\":"<<bar.low<<",\"c\":"<<bar.close<<",\"volume\":"<<bar.qty<<",\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
			bar.open=0.0;
			bar.high=0.0;
			bar.low=0.0;
			bar.close=0.0;
			bar.ltp = 0.0;
			bar.qty = 0.0;
			for(int i = 0 ;i < diff; ++i)
			{
				bar.bar_num++;
				bar.start_tm += BAR_INTERVAL;
				//send empty events;
				evtMsg<<"{\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
			}
			bar.open=bar.high=bar.low=bar.ltp=trd.price;
			bar.qty=trd.qty;
			//new bar starts
			evtMsg<<"{\"o\":"<<bar.open<<",\"h\":"<<bar.high<<",\"l\":"<<bar.low<<",\"c\":"<<bar.close<<",\"volume\":"<<bar.qty<<",\"event\":\"ohlc_notify\",\"symbol\":"<<trd.sym<<",\"bar_num\":"<<bar.bar_num<<"}"<<endl;
		 }
	}
	return evtMsg.str();
}

/**
*Funciton to process the trade details sent in by reader thread. processes the trade data and puts the OHLC bar data message on publisher queue.
*
*@param    None
*@return   void
*/
void processor()
{
	unordered_map<string, barOHLC> ohlcBar;
	trade trd;
   	while(readFinished == false)
   	{
		{
			unique_lock<mutex> lock(mxJsonReader);
			if(tradeQ.size() == 0)
			{
				cvJsonReader.wait(lock);
			}
			trd = tradeQ.front();
			tradeQ.pop();
		}
		
		string msg = createBar(ohlcBar, trd);

		lock_guard<mutex> lock(mxPublisher);
		pubQ.push(msg);
		cvPublisher.notify_all();
   	}
	
	while(!tradeQ.empty())
	{
		trd = tradeQ.front();
		tradeQ.pop();
		string msg = createBar(ohlcBar,trd);
		
		lock_guard<mutex> lock(mxPublisher);
		pubQ.push(msg);
		cvPublisher.notify_all();
	}
   	processFinished = true;
}

/**
*Funciton to publish the message.
*
*@param  None
*@return void
*/
void publisher()
{
   	while(processFinished == false)
   	{
		string msg;
		{
			unique_lock<mutex> lock(mxPublisher);
			if(pubQ.size() == 0)
			{
				cvPublisher.wait(lock);
			}
			msg=pubQ.front();
			pubQ.pop();
		}
		cout<<msg;
   	}

   	while(!pubQ.empty())
   	{
      	cout<<pubQ.front();
		pubQ.pop();
   	}
}

/**
*entry point function for the executable. It will trigger threads to read, process and publish the OHLC bar data.
*
*@param  argc	number of arguments passed to main .
*@param  argv	pointer to arguments passed to main.
*@return nonzero on error else 0.
*/
int main(int argc, char **argv)
{
	if(argc < 2)
	{
		cout<<"Argument missing for trade data filename"<<endl;
		cout<<"USAGE : ./OHLC <trade_data_filename>"<<endl;
		exit(2);
	}
	string filename(argv[1]);	
	fstream tfile;
   	tfile.open(filename, ios::in);
	if(!tfile.is_open())
	{
		cout<<"Error opening file : "<<filename<<endl;
		cout<<"ABORTING !!!"<<endl;
		exit(2);
	}
	tfile.close();

   	thread t1(reader, filename);
   	thread t2(processor);
  	thread t3(publisher);

   	t1.join();
   	t2.join();
  	t3.join();
   	return 0;
}
