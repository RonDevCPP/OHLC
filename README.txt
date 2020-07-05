=========
COMPILING 
=========
How to compile?
This project is developed on unix platform using gmake utility. To compile one needs gmake/make utility installed on unix platform.
Below is the command to compile the project.

cd OHLC
make

=======================
Running the OHLC binary
=======================
once the compilation is sucessful.A binary file named OHLC will be created in the project directory .
Enter below command at console to run the binary.

./OHLC <trade_data_filename>

OHLC takes 1 argument for trade data filename. If filename argumemt is missing binary will log error and abort.

=======
DESIGN
=======
OHLC is designed to run as standalone application which will process the trades data and publish on console.

I have made following assumptions while coding.
1. TS2 . to be received in ordered manner. If the timestamp is unordered the application will create incorrect bar data.
2. Trade data such as qty, price will not contain alphabets or any other special character else program will crash for invalid_argument.

there are three parts to OHLC . a.)reader, b.)processor and c.)publisher.
a.) Reader 
reads the trade data in JSON format and puts on the processor queue.

Parsing JSON string in c++.
I have written basic parsing for JSON string in C++. please find the pseudo code as below.
i.   remove the begining and ending curly brackets '{' '}'
ii.  split string for delimiter ',' 
iii. for each string received in step ii. break each string for delimiter ':' and interpret the first part for key and later as value
	example:- sym:"ZXXXHTUSD"  .  key will be sym and its value will be "ZXXXHTUSD"
iv.  identify key sym,P,Q,TS2 and populate in trade structure.

once the trade data is transformed to native structure . it is put on the processor queue for further processing. push operations is synchronized through mutex and condition_variable.


b.) processor 
processes the trade data and computes the bar data for OHLC .

reads trade data from processor queue then creates and maintains a map of symbol for its OHLC data. for every trade received by processor it will update the symbol for open, high, low, volume details in map. for every trade received the bar data of respective symbol is updated and the event message is created and pushed to publisher queue. read and push operation is synchronized through mutex and condition_variable.

c.) publisher
publishes/Logs the OHLC messages to console from publisher queue. read operation is synchronized through mutex and condition_variable.


