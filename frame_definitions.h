#ifndef FRAME_DEFINITIONS_H_
#define FRAME_DEFINITIONS_H_

// Node<...>  means node uses this code
enum ServerNodeCode : unsigned
{
    NodeConnect = 100,
    NodeDisconnect = 110,
    NodeFileListRequest = 120,
    NodeListAnswer = 121,
    NodeOwnerListRequest = 130,
    ServerOwnerListAnswer = 131,
    ServerFileListRefreshRequest = 140,
    NodeFileListRefreshRequest = 141,
    NodeNewFileAdded = 150,
    ServerNewFileAddedRes = 151,
    NodeFileDownloaded = 160,
    NodeFileDisclaim = 170,
};

enum NodeNodeCode : unsigned
{
    ConnectionStart = 200,
    ChunkDemand = 210,
    ChunkResponse = 211,
    NoSuchFile = 212,
    ConnectionEnd = 220,
};







#endif /* FRAME_DEFINITIONS_H_ */
