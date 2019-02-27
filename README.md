# PyHelpers
### Python Helper Extensions, a 'pyhelpers' Module 

#### INSTALLING 
- builds and works with: Python2.7, Python3.7, PyPy2. (linux)
  > pip install --upgrade https://github.com/kashirin-alex/PyHelpers/archive/master.zip


## class UdpHandlerDest in module pyhelpers.udp_handler
 A Detached and Non Blocking helper to handle sendto and recvfrom for a udp socket. Saves python's thread time.
 The name of class UdpHandlerDest "dest" is after the methods used that are based on communication with a destination address
##### Good use cases:
  * handling outgoing buffers for DNS responses.
  * instead use of many fds on python poll, use one eventfd to get the incoming queue state .


##### USING
UdpHandlerDest class Initializer , several initializations are allowed.

 * UdpHandlerDest(fileno, send_reactors=1, send_flags=0, event_fd=-1, recv_reactors=0, recv_sockets_a_reactor=4,debug_level=0)
    
   * socket_fileno, a socket fileno from which a socket is a dup 
   * send_reactors=1, the number of sending reactors(threads) reading from the dispatching queue and doing sendto
   * send_flags=0, the flag to be applied to the sendto, such as DONTWAIT(0x40) | 
   * event_fd=-1, the eventfd to use for writing recved count, used for blocking the eventfd.receive counts
   * recv_reactors, the number of reactor act to epoll fd events and do recvfrom, add to received queue and write event count
   * recv_sockets_a_reactor, number of fds created for each reactor, 
   * debug_level, 0-6, 0: none 
   
 * the handler does not do recvfrom handling in-case event_fd is "-1" or recv_reactors is "0" 
 * The running number of threads of the handler in total are recv_reactors + send_reactors + Epoll(thread)
 
```python

from pyhelpers.udp_handler import UdpHandlerDest
import socket
from eventlet import EventFd, greenthread  # use of eventlet EventFd module for use example

# configure the socket as necessary
udp_sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
udp_sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
udp_sock.bind(('::', port))  # ok to bind to both IPv4 & IPv6

event_fd = EventFd(semaphore=False)

handler = UdpHandlerDest(udp_sock.fileno(),
                         send_reactors=16, send_flags=0x40,
                         event_fd=event_fd.dup(), recv_reactors=8, recv_sockets_a_reactor=16,
                         debug_level=6)
udp_sock.close()  # socket can be close if not needed in python scope


def process_req(req):
    c_addr, c_port, data, delay = req
    
    # .... data - process .... #
    
    # during run time dispatch the client's ipv4/6 and port with the data for sending
    #   as long as the address is received on the same socket config the dispatcher can handle the sendto
    handler.dispatch(c_addr, c_port, data)
    #

# .. a while loop .. #
def run_udp_handler():
    while 1:
        count = event_fd.receive()
        for v in range(count):
            req = handler.get_recved()
            # get_recved: return empty tuple() or (c_addr, c_port, data, delay_ns)
            if not req:
                continue
            greenthread.spawn_n(process_req, req)
greenthread.spawn_n(run_udp_handler)
#


# to check on the handler's queue size state
q_send_size = handler.queued_send()  # the queued for sending
q_recv_size = handler.queued_recved()  # the queued of received

# to check on the handler's send avg (ns) latency, arg reset=bool whether to reset stats
avg_send_ns = handler.stats_send_avg(reset=True)
# to have the received calculated, it to sum-up the request's delay and divide by number of requests 

# Important to stop the UdpDispatcher handler as no longer required. (stops the detached threads and closes open fds)
handler.shutdown()

```

#### some evaluation statistics:
   * ≅20ns, the time takes for eventfd's write & read, based on eventlet.EventFd calls 
   * ≅50us, the stats_send_avg time to dispatch with sendto
   * ≅250us, sum(delays)/num_requests, the time request was waiting in the received queue


