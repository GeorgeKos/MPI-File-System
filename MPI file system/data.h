#include <queue>
#include <vector>

enum class request_type { UPLOAD,
                          UPDATE,
                          RETRIEVE };
enum class message_type { SERVER,
                          CLIENT,
                          START_LEADER_ELECTION,
                          LEADER_ELECTION_DONE,
                          ACK,
                          CANDIDATE_ID,
                          CONNECT,
                          REQUEST_SHUTDOWN,
                          SHUTDOWN_OK,
                          UPLOAD,
                          UPLOAD_ACK,
                          UPLOAD_OK,
                          UPDATE,
                          RETRIEVE,
                          UPDATE_FAILED,
                          UPLOAD_FAILED,
                          RETRIEVE_ACK,
                          RETRIEVE_OK,
                          RETRIEVE_FAILED,
                          VERSION_CHECK };
enum class role { COORDINATOR,
                  SERVER_PROCEDURE,
                  CLIENT_PROCEDURE };
class record;
class request;
int pending = 0;
//struct que
class request {
   public:
    int id;             // id of client that made the request
    int count;          // number of servers that have NOT completed the request when 0 the request is completed
    request_type type;  // the type of the request { UPLOAD,UPDATE,RETRIEVE }
    int version;        // the latest version in file system
    request(int id, int count, request_type type, int version) : id(id), count(count), type(type), version(version){};
};

class record {
   public:
    int key;
    std::queue<request>* request_queue;
    request* front;
    request* back;
    record(int key) : key(key) {
        request_queue = new std::queue<request>;
        front = &request_queue->front();
        back = &request_queue->back();
    };
};
