
/* AUTHOR Kashirin Alex (kashirin.alex@gmail.com) */

#include <iostream>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <chrono>
#include <array>

#include <pybind11/pybind11.h>
namespace py = pybind11;

namespace PyHelpers {


// QueueItemMsg
struct QueueItemMsg {
  std::string   addr;
  int           port;
  std::string   data;
  int64_t       ts_ns;
};


// Blockable Thread-Safe Queue<ItemType>
template <class T>
class Queue : std::enable_shared_from_this<Queue<T>>{

  public:

    Queue(){}

    size_t size(){
      std::lock_guard<std::mutex> lock(m_mutex);
      return m_queue.size();
    }

    T pop_first(){
      std::lock_guard<std::mutex> lock(m_mutex);
      
      T item;
      if(m_queue.size() == 0)
        return item;
      
      item = *(m_queue.front().get());
      m_queue.erase(m_queue.begin());
      return item;
    }


    void push_back(T item){
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::make_shared<T>(item));
      }
      m_blocker_cv.notify_one();
    }

    void should_block(){
      std::unique_lock<std::mutex> lock(m_blocker);

      while(!m_stopping&&size() == 0)
        m_blocker_cv.wait(lock);
    }

    void stopping(){
      m_stopping.store(true);
      m_blocker_cv.notify_all();
    }

    virtual ~Queue(){}

  private:
    std::mutex m_mutex;
    std::vector<std::shared_ptr<T>> m_queue;
    
    std::condition_variable m_blocker_cv;
    std::mutex m_blocker;

    std::atomic<bool> m_stopping = false;

};
typedef std::shared_ptr<Queue<QueueItemMsg>>  QueueMsgPtr;


struct DestAddr {
  struct sockaddr* addr;
  socklen_t        len;
  
  struct sockaddr_in  da4 = {0};
  struct sockaddr_in6 da6 = {0};
};

int64_t ts_ns_now(){
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
     std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

bool make_socket_nonblocking(int socketfd) {
  int flags = fcntl(socketfd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "[E] fcntl failed (F_GETFL)\n";
    return false;
  }
  if(flags & O_NONBLOCK)
    return true;

  flags |= O_NONBLOCK;
  int s = fcntl(socketfd, F_SETFL, flags);
  if (s == -1) {
    std::cerr << "[E] fcntl failed (F_SETFL)\n";
    return false;
  }
  return true;
}

// UdpDispatcher
class UdpDispatcher : std::enable_shared_from_this<UdpDispatcher>{

  public:

    UdpDispatcher(int sock_fileno, int snd_flags,  QueueMsgPtr queue, int debug):
                  m_snd_flags(snd_flags), m_queue(queue), m_debug(debug) {
      
      m_fd = dup(sock_fileno),
      m_run.store(true);
    }

    void run(){

        while(1){
          m_queue->should_block();

          if(!m_run.load()) 
            break;

          dispatch_item(m_queue->pop_first());
          
          if(errno == EBADF || errno == ENOTCONN)
            break;
        }

        try { close(m_fd); } catch (...) {}
    }

    void shutdown(){
      m_run.store(false);
    }

    virtual ~UdpDispatcher(){
      shutdown();
    }

    std::atomic<uint32_t>  m_stat_sendto_c=0;
    std::atomic<uint32_t>  m_stat_sendto_avg=0;

    std::atomic<bool> m_run=false;

  private:

    void dispatch_item(QueueItemMsg msg){
      
      if(msg.addr.empty()) return;

      if(m_debug >= 5)
        std::cout << "UdpDispatcher dispatch_item, " 
                  << " addr " << msg.addr 
                  << " port " << msg.port 
                  << " data " << msg.data 
                  << " ts_ns " << msg.ts_ns 
                  << "\n";

      errno = 0;
          
      DestAddr to = {0};
      set_dest_addr(msg.addr, htons(msg.port), &to);

      if(errno == 0 && sendto(m_fd, msg.data.c_str(), msg.data.length(), 
                              m_snd_flags, to.addr, to.len)  > -1){

        m_stat_sendto_avg.store((m_stat_sendto_avg * m_stat_sendto_c + (ts_ns_now() - msg.ts_ns)) 
                                / (m_stat_sendto_c + 1));
        m_stat_sendto_c++;
        return;
      }

      std::cerr << "Error dispatch_item: " << msg.addr << ":" << msg.port 
                << " data-sz: " << msg.data.length()
                << " error: " << errno << " " << strerror(errno) << "\n";
    }

    static void set_dest_addr(std::string addr, in_port_t port, DestAddr *to) {
      
      if(inet_pton(AF_INET, addr.c_str(), &to->da4.sin_addr.s_addr) == 1){
        to->da4.sin_family=AF_INET;
        to->da4.sin_port=port;
        to->len = sizeof(to->da4);
        to->addr = (struct sockaddr*)&to->da4;
        return;
      }
      if (inet_pton(AF_INET6, addr.c_str(), &to->da6.sin6_addr) == 1){
        to->da6.sin6_family=AF_INET6;
        to->da6.sin6_port=port;
        to->len = sizeof(to->da6);
        to->addr = (struct sockaddr*)&to->da6;
        return;
      }

    }


    int m_fd, m_snd_flags;
    QueueMsgPtr m_queue;

    int m_debug;

};
typedef std::shared_ptr<UdpDispatcher> UdpDispatcherPtr;
// UdpDispatcher end



// UdpReceiver
class UdpReceiver : std::enable_shared_from_this<UdpReceiver>{

  public:

    UdpReceiver(int sock_fileno, int recv_sockets_a_reactor,  
                QueueMsgPtr q_received, int ev_fileno, int debug):
                m_ev_fd(ev_fileno), 
                m_queue_received(q_received), 
                m_debug(debug) {
      m_max_evs = recv_sockets_a_reactor;
      
      // Get size for receiving buffer
      m_rcvbuff = 512;
      socklen_t optlen = sizeof(m_rcvbuff);
      if(getsockopt(sock_fileno, SOL_SOCKET, SO_RCVBUF, &m_rcvbuff, &optlen) == -1)
        std::cerr << "Error getsockopt: SO_RCVBUF ";
        

      m_poll_fd = epoll_create(recv_sockets_a_reactor);
      if (m_poll_fd == -1) 
        std::cerr << "Error epoll_create";

      struct epoll_event ev;
      for(int t=0; t < recv_sockets_a_reactor; t++){
        ev = {};
        ev.events = EPOLLIN;
	      ev.data.fd = dup(sock_fileno);   
        m_socks.push_back(ev.data.fd);

        make_socket_nonblocking(ev.data.fd);
	      if (epoll_ctl(m_poll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) == -1)
            std::cerr << "Error epollf_ctl:" << m_poll_fd << "\n";
      }
      
      m_run.store(true);
    }

    void run(){

      bool rcv;
      uint64_t  count = 0;
      int ev_sent;

      int nfds;
      
      struct epoll_event *events;
      events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * m_max_evs);
      memset(events, 0, sizeof(struct epoll_event) * m_max_evs);

      while(m_run.load()){
        errno = 0;
        nfds = epoll_wait(m_poll_fd, events, m_max_evs, 5);
	      if (nfds == -1 && errno == EBADF) {
          std::cerr << "Error epoll_wait: " << errno << " " << strerror(errno) << "\n";
			    break;
		    }

		    for (int n = 0; n < nfds; ++n) {
          if (events[n].events & EPOLLIN) {
            
            if(m_debug >= 4)
              std::cout << "epoll_wait, " << "EPOLLIN " << events[n].data.fd << "\n";
            
            errno = 0; 
            rcv = receive(events[n].data.fd);
        
            if(m_debug >= 5 && errno != 0)
              std::cout << "UdpReceiver run, " << "fd " << events[n].data.fd 
                        <<"errno " << errno << " " << strerror(errno) << "\n";
            if(errno == EBADF || errno == ENOTCONN)
              break;

            if(!rcv)
              continue;
          
            count++;
            if(count >= INT64_MAX )
              count = 1;
            errno = 0; 
            if((ev_sent = write(m_ev_fd, &count, sizeof(uint64_t))) > 0)
              count--;
            if(errno != 0)
              std::cerr << "Error ev_write: " << errno << " " << strerror(errno) << "\n";

            if(ev_sent == -1 && (errno == EBADF || errno == ENOTCONN))
              break;
          }
        }
      }

      free(events);
      
      try { close(m_poll_fd); } catch (...) {} 
      for(int fd : m_socks)
        try { close(fd); } catch (...) {} 

      shutdown();
    }

    void shutdown(){
      m_run.store(false);
    }

    virtual ~UdpReceiver(){
      shutdown();
    }

    std::atomic<bool> m_run = false;

  private:
    
    bool receive(int sockfd){

      char buf[m_rcvbuff];
      // memset(buf, 0, sizeof(buf));

      struct sockaddr_storage caddr;
      socklen_t caddrlen = sizeof(caddr);

      ssize_t nbytes;
      
      if((nbytes = recvfrom(sockfd, (char *)buf, m_rcvbuff, 0, (struct sockaddr *) &caddr, &caddrlen)) < 1){
        return false;
      }

      QueueItemMsg msg = {};

      char c_addr[INET6_ADDRSTRLEN];
      switch(caddr.ss_family){
        case AF_INET: {
          inet_ntop(AF_INET, &((struct sockaddr_in *)&caddr)->sin_addr,  c_addr, INET6_ADDRSTRLEN);
          if(c_addr == nullptr) 
            return false;
          msg.port = ntohs(((struct sockaddr_in *)&caddr)->sin_port);
          break;
        }
        case AF_INET6:{
          inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&caddr)->sin6_addr, c_addr, INET6_ADDRSTRLEN);
          if(c_addr == nullptr) 
            return false;
          msg.port = ntohs(((struct sockaddr_in6 *)&caddr)->sin6_port);
          break;
        }
       default:
         return false;
      }

      msg.addr = c_addr;
      msg.data = std::string(buf, nbytes);
      msg.ts_ns = ts_ns_now();
      m_queue_received->push_back(msg);

      if(m_debug >= 5)
        std::cout << "UdpReceiver receive, " 
                  << " addr " << msg.addr 
                  << " port " << msg.port 
                  << " data " << msg.data 
                  << " ts_ns " << msg.ts_ns 
                  << "\n";
      return true;
    }

    int m_ev_fd, m_rcvbuff;
    QueueMsgPtr m_queue_received;

    int m_poll_fd, m_max_evs;
    std::vector<int> m_socks;

    int m_debug;
    
};
typedef std::shared_ptr<UdpReceiver> UdpReceiverPtr;
// UdpReceiver end



class UdpHandlerDest {

  public:

    UdpHandlerDest(int sock_fileno, 
                   int send_reactors, int send_flags, 
                   int ev_fileno, int recv_reactors, int recv_sockets_a_reactor,
                   int debug) : m_debug(debug) {
      
      // UdpDispatcher threads setup
      if(send_reactors > 0){
        UdpDispatcherPtr d_ptr;
        for(int t=0;t<send_reactors;t++){ 
          d_ptr = std::make_shared<UdpDispatcher>(sock_fileno, send_flags, m_queue_dispatch, debug);
          std::thread ( [d=d_ptr]{ d->run(); } ).detach();
          m_dispatchers.push_back(d_ptr);
        }
      }

      // UdpReceiver threads setup
      if(recv_reactors > 0 && ev_fileno > 0){
        UdpReceiverPtr r_ptr;
        for(int t=0;t<recv_reactors;t++){
          r_ptr = std::make_shared<UdpReceiver>(sock_fileno, recv_sockets_a_reactor, 
                                                m_queue_received, ev_fileno, debug);
          std::thread ( [d=r_ptr]{ d->run(); } ).detach();
          m_receivers.push_back(r_ptr);
        }
      }

      // let the thread instances to initiate  
     
      bool wait;
      do{
        wait = false;
        if(wait)
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        
        for(auto &d : m_dispatchers){
          if(d->m_run.load())  continue;
          wait = true;
          break;
        }
        for(auto &d : m_receivers){
          if(d->m_run.load())  continue;
          wait = true;
          break;
        }
      } while (wait);
    }

   // DISPATCH
    void dispatch(std::string addr, int port, std::string data){
      QueueItemMsg msg = {
        .addr = addr,
        .port = port,
        .data = data,
        .ts_ns = ts_ns_now()
      };
      m_queue_dispatch->push_back(msg);
      
      if(m_debug >= 5)
        std::cout << "UdpHandlerDest dispatch, " 
                  << " addr " << msg.addr 
                  << " port " << msg.port 
                  << " data.length " << msg.data.length()
                  << " ts_ns " << msg.ts_ns 
                  << "\n";
    }

    size_t queued_send(){
      return m_queue_dispatch->size();
    }

    u_int32_t stats_send_avg(bool reset){
      u_int32_t sum = 0, count = 0, c;

      for(auto &d : m_dispatchers){
          c = d->m_stat_sendto_c.load();
          count += c;
          sum += d->m_stat_sendto_avg.load()*c;

          d->m_stat_sendto_c.store(0);
          d->m_stat_sendto_avg.store(0);
      }
      if(count==0)return 0;
      return sum/count;
    }
    // DISPATCH


    // RECEIVE
    QueueItemMsg get_recved(){
      return m_queue_received->pop_first();
    }

    size_t queued_recved(){
      return m_queue_received->size();
    }
    // RECEIVE


    operator UdpHandlerDest*() { 
      return this;
    }

    void shutdown(){
      if(m_debug >= 1)
        std::cout << "UdpHandlerDest shutting down" << "\n";

      for(auto &d : m_dispatchers)
          d->shutdown();
      m_queue_received->stopping();

      for(auto &d : m_receivers)
          d->shutdown();
      m_queue_dispatch->stopping();

      if(m_debug >= 1)
        std::cout << "UdpHandlerDest shutdown" << "\n";
    }

    virtual ~UdpHandlerDest(){
      shutdown();
    }

  private:

    std::vector<std::shared_ptr<UdpDispatcher>> m_dispatchers;
    QueueMsgPtr m_queue_dispatch = std::make_shared<Queue<QueueItemMsg>>();

    std::vector<std::shared_ptr<UdpReceiver>> m_receivers;
    QueueMsgPtr m_queue_received = std::make_shared<Queue<QueueItemMsg>>();
  
    int m_debug;
};


}  // namespace PyHelpers



PYBIND11_MODULE(udp_handler_dest, m) {
  m.doc() = "Python Helper for receiving and sending udp messages without blocking/waiting";

  py::class_<PyHelpers::UdpHandlerDest, std::shared_ptr<PyHelpers::UdpHandlerDest>>(m, "UdpHandlerDest")
    .def(py::init<int, int, int, int, int, int, int>(),        
        py::arg("sock_fd"), 
        py::arg("send_reactors") = 1, 
        py::arg("send_flags") = 0,       
        py::arg("event_fd") = -1, 
        py::arg("recv_reactors") = 0,
        py::arg("recv_sockets_a_reactor") = 4,
        py::arg("debug_level") = 0
        )

    .def("dispatch", &PyHelpers::UdpHandlerDest::dispatch, py::arg("ip"), py::arg("port"), py::arg("data") )
    
    .def("get_recved", [](PyHelpers::UdpHandlerDest* hdlr) {
      PyHelpers::QueueItemMsg msg = hdlr->get_recved();
      if(msg.addr.empty()) 
        return py::make_tuple();
      return py::make_tuple(py::bytes(msg.addr), msg.port, py::bytes(msg.data), PyHelpers::ts_ns_now()-msg.ts_ns);
    })

    .def("queued_send", &PyHelpers::UdpHandlerDest::queued_send)
    .def("stats_send_avg",  &PyHelpers::UdpHandlerDest::stats_send_avg,  py::arg("reset") = true)

    .def("queued_recved",  &PyHelpers::UdpHandlerDest::queued_recved)
    
    .def("shutdown", &PyHelpers::UdpHandlerDest::shutdown)
  ;
}