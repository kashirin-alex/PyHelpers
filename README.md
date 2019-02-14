# PyHelpers
### Python Helper Extensions, a 'pyhelpers' Module 

#### INSTALLING 
  > pip install --upgrade https://github.com/kashirin-alex/PyHelpers/archive/master.zip
  


## class UdpDispatcher in module pyhelpers.udp_dispatcher 
 A Detached and Non Blocking helper to handle sendto() outgoing buffers for a udp socket. Saves python's thread time. 
##### Good use cases:
  * handling outgoing buffers for DNS responses.


##### USING 
```python

from pyhelpers.udp_dispatcher import UdpDispatcher

# .... #
# After a socket has been initialized, 
#   pass it's fileno to UdpDispatcher class init with desired flags (a dup is issued on the fileno)
dispatcher = UdpDispatcher(udp_sock.fileno(), 0x40)

# .. a while loop .. #

data, address = udp_sock.recvfrom(512)

# .... data - process .... #

# during run time push the client's ipv4/6 and port with the data for sending
#   as long as the address is received on the same socket config the dispatcher can handle the sendto
dispatcher.push(address[0], address[1], data)

# to check on the dispatcher's queue size
q_size = dispatcher.queued()

# Important to stop the UdpDispatcher handler(a detached thread and to close it's fd ) as no longer required
dispatcher.shutdown()

```

