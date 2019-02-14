
/* AUTHOR Kashirin Alex (kashirin.alex@gmail.com) */

#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include <vector>
#include <string>
#include <atomic>
#include <mutex>

#include <pybind11/pybind11.h>
namespace py = pybind11;

namespace PyHelpers {

struct QueueItem {
  std::string   addr;
  int           port;
  std::string   data;
};

struct DestAddr {
  struct sockaddr* addr;
  socklen_t        len;
  
  struct sockaddr_in  da4 = {0};
  struct sockaddr_in6 da6 = {0};
};

class UdpDispatcher{

  public:

    UdpDispatcher(int fileno, int flags):  m_flags(flags) {
      m_fd = dup(fileno);
      try {
          std::thread ([this] { this->dispatch(); }).detach();
      } catch (std::exception& e) {
          std::cerr << e.what();
      } catch (...) {
          std::cerr << "Error Uknown Initializing UdpDispatcher";
      }

    }

    void push(std::string addr, int port, std::string data){
      std::lock_guard<std::mutex> lock(m_q_mutex);	
      
      QueueItem item;
      item.addr = addr;
      item.port = port;
      item.data = data;
      m_queue.push_back(item);

      m_blocker.unlock();
    }

    size_t queued(){
      std::lock_guard<std::mutex> lock(m_q_mutex);
      return m_queue.size();
    }

    void shutdown(){
      m_run.store(false);
      m_blocker.unlock();
    }

    operator UdpDispatcher*() { 
      return this;
    }

    virtual ~UdpDispatcher(){
      shutdown();
    }

  private:

    void dispatch(){

        if (queued() == 0)
          m_blocker.lock();
        
        while(1){
          m_blocker.lock();
        
          if(!m_run.load()) 
            break;

          dispatch_item(get_item());
          
          if(errno & (EBADF | ENOTCONN)) 
            break;

          if(queued()>0)
            m_blocker.unlock();
        }

        try { close(m_fd); } catch (...) {}
    }

    QueueItem get_item(){
      std::lock_guard<std::mutex> lock(m_q_mutex);	

      QueueItem item = m_queue.front();
      m_queue.erase(m_queue.begin());
      return item;
    }

    void dispatch_item(QueueItem item){
      errno = 0;
      DestAddr to = {0};
      set_dest_addr(item.addr, htons(item.port), &to);
      
      if(errno == 0 && sendto(m_fd, item.data.c_str(), item.data.length(), m_flags, to.addr, to.len)  > -1)
          return;

      std::cerr << "Error dispatch_item: " << item.addr << ":" << item.port 
                << " data-sz: " << item.data.length()
                << " error: " << errno << " " << strerror(errno) << "\n";
    }

    void set_dest_addr(std::string addr, in_port_t port, DestAddr *to) {
      
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

    int m_fd;
    int m_flags;
    
    std::vector<QueueItem> m_queue;
    std::mutex m_q_mutex;	
    std::mutex m_blocker;

    std::atomic<bool> m_run=true;
};


}  // namespace PyHelpers



PYBIND11_MODULE(udp_dispatcher, m) {
  m.doc() = "Python Helper to dispatch udp messages without blocking/waiting";

  py::class_<PyHelpers::UdpDispatcher, std::unique_ptr<PyHelpers::UdpDispatcher, py::nodelete>>(m, "UdpDispatcher")
    .def(py::init<int , int>(), py::arg("fileno"), py::arg("flags") = NULL )
    .def("push", &PyHelpers::UdpDispatcher::push, py::arg("ip"), py::arg("port"), py::arg("data") )
    .def("queued", &PyHelpers::UdpDispatcher::queued)
    .def("shutdown", &PyHelpers::UdpDispatcher::shutdown)
  ;
}

