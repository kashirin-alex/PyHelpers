# -*- coding: utf-8 -*-
# AUTHOR Kashirin Alex (kashirin.alex@gmail.com) #

from pyhelpers.udp_dispatcher import UdpDispatcher
import socket
import psutil
import time
import threading

host_ip = "127.0.0.1"
host_port = 1100
original_udp_send = False
with_req_load = False
num_threads = 8
test_duration = 10  # secs

p = psutil.Process()
udp_socks = []
ports = []
dispatchers = []
threads = []
conf = {'d': None, 'c': 0}
integrity = {}
to_run = True
client_af = socket.AF_INET if '.' in host_ip else socket.AF_INET6

send_bytes_clt = 32
send_bytes_srv = 1024*send_bytes_clt
recv_buf_clt = 32*1024
send_buf_srv = 32*1024


def proc_state():
    print ('num_threads', p.num_threads(), 'cpu_percent', p.cpu_percent(),
           'queued', conf['d'].queued() if not original_udp_send and conf['d'] is not None else 0,
           'avg', conf['d'].stats_avg(True) if not original_udp_send and conf['d'] is not None else 0)
    #

#


def run_srv_sock(tn, run):
    print ('running py thread', 'srv', tn)

    udp_s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    udp_s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, send_buf_srv)
    udp_s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 512)
    udp_s.settimeout(5)

    port = host_port
    udp_s.bind(('::', port))
    ports.append(port)
    udp_socks.append(udp_s)

    if not original_udp_send and conf['d'] is None:
        conf['d'] = UdpDispatcher(udp_s.fileno(), 0x40)
    dispatcher = conf['d']

    while run():
        try:
            data, address = udp_s.recvfrom(512)
            if with_req_load:
                tmp_load = ''.join([str(n) for n in range(10000)])
            integrity[data]['s'] = len(data)
            if original_udp_send:
                udp_s.sendto(data.zfill(send_bytes_srv), 0x40, address)
            else:
                dispatcher.push(address[0], address[1], data.zfill(send_bytes_srv))
        except Exception as e:
            print ('srv', tn, e)
            break

    udp_s.close()
    print ('stopped py thread', 'srv', tn, dispatcher.queued() if not original_udp_send else 0)
    #


proc_state()
for n in range(1, num_threads+1):
    threading.Thread(target=run_srv_sock, name=str(n), args=[n, lambda: to_run]).start()
proc_state()


#


def run_clt_sock(tn, addr, run):
    print ('running py thread', 'clt', tn, addr)
    udp_s = socket.socket(client_af, socket.SOCK_DGRAM)
    udp_s.settimeout(5)
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

proc_state()
clt_n = 0
for srv_port in ports:
    for n in range(1, num_threads+1):
        clt_n += 1
        threading.Thread(target=run_clt_sock, name=str(clt_n),
                         args=[clt_n, (host_ip, srv_port), lambda: to_run]).start()
proc_state()


#
# WAIT
for w in range(0, num_threads):
    time.sleep(test_duration/num_threads)
    proc_state()

if not original_udp_send and conf['d'].queued() > 0:
    proc_state()
    time.sleep(0.001*conf['d'].queued())

time.sleep(3)
to_run = False
time.sleep(3)

for s in udp_socks:
    try:
        s.close()
    except:
        pass
    time.sleep(0)
time.sleep(3)
#

if not original_udp_send:
    print ('*'*80)
    proc_state()
    time.sleep(3)
    conf['d'].shutdown()
    time.sleep(3)
    proc_state()
    print ('*'*80)


time.sleep(3)
f = True
for n in integrity:
    if send_bytes_srv != integrity[n]['c'] and send_bytes_clt == integrity[n]['s']:
        f = False
        print ("clt_n", n.split(b'|')[0], "recv", integrity[n])

time.sleep(3)
f = f and integrity and p.num_threads() == 1


print ('*'*80)
print ("TEST: "+("PASS" if f else "FAIL")+"!", 'trx', len(integrity), 'num_threads', p.num_threads())
print ('*'*80)

if not f:
    exit(1)

#               original_udp_send | dispatcher
# py-2 trx      863544              544415
# py-3 trx      905657              363847
# pypy trx      229255              171673

#
