/****
	*this file is designed for communication with Coordinator
	*
	*
	*
	**/
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/syscall.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <functional>
#include "hotstuff/util.h"
#include "IOTA_communication/serial.h"
#define connect_pool 20 
#define buffer_size 2048
class Coo{
public:
	using deal_cb = std::function<void(unsigned int, uint8_t*)>; 
	Coo(deal_cb deal, int port);
	static void *init_listen(void *deal);
	//default host "127.0.0.1"
	static bool send_data(int port, uint8_t* data, int length);
	bool listen_on_iri(int port);
	static bool listening_iri(bool& ans);
private:
	static int listen_fd_iri;
	static struct sockaddr_in  server_sockaddr_iri, client_addr_iri;
	static int listen_port;
	static int conn_fd;
	deal_cb fun_deal;
	pthread_t coo_tid;
	//define socket server address when it represent client
};
struct iota_config{
	std::string host;
	int port;
};
