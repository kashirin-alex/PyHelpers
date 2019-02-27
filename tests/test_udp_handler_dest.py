# -*- coding: utf-8 -*-
# AUTHOR Kashirin Alex (kashirin.alex@gmail.com) #

from pyhelpers.udp_handler_dest import UdpHandlerDest
import eventlet
from eventlet import EventFd, greenthread
import socket
import psutil
import time
ev_sleep = eventlet.sleep

host_ip = "127.0.0.1"
host_port = 1100


with_req_load = False
num_threads = 8

clt_threads = 4
srv_greenthreads_process = 8
recv_reactors  = 2
recv_sockets_a_reactor = 4
send_reactors = 2

test_duration = 60  # secs

p = psutil.Process()
udp_socks = []
ports = []
dispatchers = []
threads = []
conf = {'c': 0, 'queued_recved.sum': 0, 'queued_recved.c': 0}
integrity = {}
to_run = True
client_af = socket.AF_INET if '.' in host_ip else socket.AF_INET6

send_bytes_clt = 32
send_bytes_srv = 1024*send_bytes_clt
recv_buf_clt = 32*1024
send_buf_srv = 32*1024


def get_stats_recv_avg():
    if conf['queued_recved.c'] == 0:
        return 0
    return conf['queued_recved.sum']/conf['queued_recved.c']


def proc_state():
    print ('num_threads', p.num_threads(), 'cpu_percent', p.cpu_percent(),
           'queued_send', handler.queued_send(), 'queued_recved', handler.queued_recved(),
           'stats_send_avg', handler.stats_send_avg(True),
           'stats_recv_avg', get_stats_recv_avg())
    #

#

udp_s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
udp_s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, send_buf_srv)
udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512)
# udp_s.settimeout(5)

port = host_port
ports.append(port)
udp_s.bind(('::', port))
udp_s.setblocking(0)

event_fd = EventFd(semaphore=False)
handler = UdpHandlerDest(udp_s.fileno(),
                         send_reactors=send_reactors, send_flags=0x40,
                         event_fd=event_fd.dup(), recv_reactors=recv_reactors,
                         recv_sockets_a_reactor=recv_sockets_a_reactor,
                         debug_level=True)
print (event_fd.fileno(), udp_s.fileno(), handler)
udp_s.close()


def process_req(req):
    c_addr, c_port, data, ns_passed = req

    if with_req_load:
        '..'.join([str(a) for a in range(8096)])
    integrity[data]['s'] = len(data)
    handler.dispatch(c_addr, c_port, data.zfill(send_bytes_srv))
    conf['queued_recved.c'] += 1
    conf['queued_recved.sum'] += ns_passed


def run_receive_events():
    try:
        print ('run_receive_events')
        while to_run:
            count = event_fd.receive()
            # print ('event_fd.receive', count)
            for v in range(count):
                req = handler.get_recved()
                if not req:
                    print ('get_recved', req)
                    continue
                greenthread.spawn_n(process_req, req)
    except Exception as e:
        print (e)
    #
greenthread.spawn_n(run_receive_events)

#


def run_clt_sock(tn, addr, run):
    print ('running py thread', 'clt', tn, addr)
    udp_s = socket.socket(client_af, socket.SOCK_DGRAM)
    udp_s.settimeout(25)
    while run():
        try:
            conf['c'] += 1
            uid = ("%d-%d|" % (conf['c'], tn)).zfill(send_bytes_clt).encode('utf-8')
            integrity[uid] = {'c': 0, 's': 0}
            udp_s.sendto(uid, addr)
            data, server = udp_s.recvfrom(recv_buf_clt)
            integrity[uid]['c'] = len(data)
        except Exception as e:
            print ('clt', tn, e)
            break
        time.sleep(0)
    udp_s.close()
    print ('stopped py thread', 'clt', tn, addr)
    #

import threading
proc_state()
clt_n = 0
for srv_port in ports:
    for n in range(1, clt_threads+1):
        clt_n += 1
        threading.Thread(target=run_clt_sock, name=str(clt_n),
                         args=[clt_n, (host_ip, srv_port), lambda: to_run]).start()
proc_state()


#
# WAIT
print ('waiting')
for w in range(num_threads):
    ev_sleep(test_duration/num_threads)
    proc_state()

q = handler.queued_send() + handler.queued_recved()
if q > 0:
    proc_state()
    ev_sleep(0.001*q)

ev_sleep(3)
to_run = False
ev_sleep(3)

for s in udp_socks:
    try:
        s.close()
    except:
        pass
    ev_sleep(0)
ev_sleep(3)
#

print ('*'*80)
proc_state()
ev_sleep(3)
handler.shutdown()
ev_sleep(10)
proc_state()
print ('*'*80)


ev_sleep(10)
f = True
for n in integrity:
    if send_bytes_srv != integrity[n]['c'] and send_bytes_clt == integrity[n]['s']:
        f = False
        print ("clt_n", n.split(b'|')[0], "recv", integrity[n])

ev_sleep(3)
f = f and integrity and p.num_threads() == 1


print ('*'*80)
print ("TEST: "+("PASS" if f else "FAIL")+"!", 'trx', len(integrity), 'num_threads', p.num_threads())
print ('*'*80)

if not f:
    exit(1)
