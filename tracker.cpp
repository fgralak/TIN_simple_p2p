#include <iostream>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <atomic>
#include <map>
#include <vector>
#include "helper_functions.h"
#include "bencode_parser.h"
#include "frame_definitions.h"

using namespace std;

// Type alias for vector of ip, port pairs
using ClientList = vector <pair<string, int>>;

// Set to true if termination of threads is needed
atomic <bool> terminateAllThreads;

// Filename -> Vector (IP, Port)
map < string, ClientList > mapping;

// Mutex for mapping
pthread_mutex_t mappingMutex;

// Function each thread executes
void* serverWorker(void*);

// Split string into parts
vector<string> split(string, char);

// Add the client details to mapping
string addToList(string, string, int);

// Remove the client details from mapping
string removeFromList(string, string, int);

// Get list of available files 
string getListOfFiles();

// Get list of clients having this file
ClientList getListOfClients(string);

int main(int argc, char* argv[])
{
    // Check if arguments are valid
    if(argc < 2)
    {
        printf("Usage: %s <Port Number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get port number from command line
    short listenPort = atoi(argv[1]);
    // Number of clients which can queue up
    const int QUEUED_LIMIT = 10;
    // Number of server threads
    const int THREAD_COUNT = 4;

    // Create listening socket and set to listen
    int listeningSocketFD = createTCPSocket();
    bindToPort(listeningSocketFD, listenPort);
    if(listen(listeningSocketFD, QUEUED_LIMIT) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    terminateAllThreads = false;
    pthread_t threadID[THREAD_COUNT];
    for(int i = 0; i < THREAD_COUNT; i++)
        pthread_create(&threadID[i], NULL, serverWorker, (void*)&listeningSocketFD);

    for(int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threadID[i], NULL);

    closeSocket(listeningSocketFD);
    return 0;
}

void* serverWorker(void* arg)
{
    printf("Server Thread %lu created\n", pthread_self());
    int listeningSocketFD = *(static_cast <int*> (arg));

    while(!terminateAllThreads)
    {
        sockaddr_in clientAddr;
        unsigned clientLen = sizeof(clientAddr);
        memset(&clientAddr, 0, clientLen);

        // Accept one client request
        int sockFD = accept(listeningSocketFD,
                            (sockaddr*) &clientAddr,
                            (socklen_t*)&clientLen);
        if(sockFD < 0)
        {
            perror("Accept failed");
            closeSocket(listeningSocketFD);
            printf("Terminating thread %lu\n", pthread_self());
            terminateAllThreads = true;
            pthread_exit(NULL);
        }

        char trackerRequest[BUFF_SIZE];
        memset(trackerRequest, 0, BUFF_SIZE);

        // Receive Tracker Request from client
        int requestMsgLen = recv(sockFD, trackerRequest, BUFF_SIZE, 0);

        // Receive failed for some reason
        if(requestMsgLen < 0)
        {
            perror("recv() failed");
            closeSocket(sockFD);
            continue;
        }

        // Connection closed by client
        if(requestMsgLen == 0)
        {
            printf("Connection closed from client side\n");
            closeSocket(sockFD);
            continue;
        }

        vector<string> request = split(trackerRequest,'$');

        BencodeParser bencodeParser(request[1]);

        if(request[0] != to_string(ServerNodeCode::NodeFileListRequest))
        {
	        printf("%s\n", request[1]);
	        bencodeParser.print_details();
	    }

        string trackerResponse = "";

        if(request[0] == to_string(ServerNodeCode::NodeNewFileAdded))
        {
            trackerResponse = addToList(bencodeParser.filename, inet_ntoa(clientAddr.sin_addr), bencodeParser.port);
        }
        else if(request[0] == to_string(ServerNodeCode::NodeOwnerListRequest))
        {
            ClientList clientList = getListOfClients(bencodeParser.filename);
            for(auto client : clientList)
            {
                trackerResponse += "6:peerip";
                trackerResponse += to_string(client.first.size()) + ":";
                trackerResponse += client.first;
                trackerResponse += "8:peerport";
                trackerResponse += "i" + to_string(client.second) + "e$";
            }
        }
        else if(request[0] == to_string(ServerNodeCode::NodeFileDisclaim))
        {
            trackerResponse = removeFromList(bencodeParser.filename, inet_ntoa(clientAddr.sin_addr), bencodeParser.port);
        }
        else if(request[0] == to_string(ServerNodeCode::NodeFileListRequest))
        {
        	trackerResponse = getListOfFiles();
        }
        else if(request[0] ==to_string(ServerNodeCode::NodeFileDownloaded)) {
            addToList(bencodeParser.filename, inet_ntoa(clientAddr.sin_addr), bencodeParser.port);
         }


        if(trackerResponse == "")
            trackerResponse = "empty";

        int responseLen = trackerResponse.size();

        if(sendAll(sockFD, trackerResponse.c_str(), responseLen) != 0)
        {
            perror("sendall() failed");
            printf("Only sent %d bytes\n", responseLen);
        }

        // Serve client here
        closeSocket(sockFD);
    }

    return NULL;
}

vector<string> split(string txt, char ch)
{
    size_t pos = txt.find(ch);
    size_t initialPos = 0;
    vector<string> strs;
    
    while(pos != string::npos) 
    {
        strs.push_back(txt.substr(initialPos, pos - initialPos));
        initialPos = pos + 1;
        pos = txt.find(ch, initialPos);
    }

    strs.push_back( txt.substr(initialPos, min(pos, txt.size()) - initialPos + 1));
    return strs;
}

string addToList(string filename, string ip, int port)
{
    // Add this client to the list
    pthread_mutex_lock(&mappingMutex);
    string response = "";
    for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
    {
        if(map_iter->first == filename)
        {
            for(auto vec_iter = map_iter->second.begin(); vec_iter != map_iter->second.end(); ++vec_iter)
            {
                if(vec_iter->first == ip && vec_iter->second == port)
                {
                    response = "Client is already on the list.";
                    break;
                }
            }
            break;
        }
    }
    if(response == "")
    {
        mapping[filename].push_back({ip, port});
        response = "Client details added to the list.";
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

string removeFromList(string filename, string ip, int port)
{
    // Add this client to the list
    pthread_mutex_lock(&mappingMutex);
    string response = "Client is not on the list.";
    for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
    {
        if(map_iter->first == filename)
        {
            for(auto vec_iter = map_iter->second.begin(); vec_iter != map_iter->second.end(); ++vec_iter)
            {
                if(vec_iter->first == ip && vec_iter->second == port)
                {
                    map_iter->second.erase(vec_iter);
                    response = "Client details removed from the list.";
                    break;
                }
            }
        }
        if(map_iter->second.size() == 0)
        {
        	mapping.erase(map_iter);
        	break;
        }
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

string getListOfFiles()
{
	// Get list of files
    pthread_mutex_lock(&mappingMutex);
    string response = "List of available files:\n";
    for(auto map_iter = mapping.begin(); map_iter != mapping.end() ; ++map_iter)
    {
        response += map_iter->first + "\n";
    }
    pthread_mutex_unlock(&mappingMutex);
    return response;
}

ClientList getListOfClients(string filename)
{
    pthread_mutex_lock(&mappingMutex);
    ClientList retVal(mapping[filename]);
    pthread_mutex_unlock(&mappingMutex);
    return retVal;
}
