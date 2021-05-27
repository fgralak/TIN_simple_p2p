#ifndef TORRENT_PARSER_H
#define TORRENT_PARSER_H

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

using namespace std;

class TorrentParser
{
public:

    string trackerIP;
    int trackerPort;
    string filename;
    int filesize;

    TorrentParser(string torrentfile = "") :
	trackerIP(""), trackerPort(-1), filename(""), filesize(0)
    {
        if(torrentfile == "")
        {
            printf("Call constructor with non NULL parameter\n");
            return;
        }
        
        ifstream fileIn(torrentfile);
        if(!fileIn.is_open())
        {
            printf("Couldn't open file %s\n", torrentfile);
            return;
        }

        string key, val;
        while(fileIn >> key >> val)
        {
            if(key == "announceip")
                trackerIP = val;

            else if(key == "announceport")
                trackerPort = stoi(val);

            else if(key == "filename")
                filename = val;

            else if(key == "filesize")
                filesize = stoi(val);
        }

        fileIn.close();
    }
};

#endif
