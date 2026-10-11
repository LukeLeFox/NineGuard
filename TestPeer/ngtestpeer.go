// ngtestpeer: peer WireGuard userspace, isolato e reversibile, per il gate
// "peer esterno controllato" di NineGuard 2.0.
//
// Gira come utente normale sul PC di sviluppo: nessun root, nessuna
// interfaccia tun del kernel, nessuna route o regola firewall. Usa
// wireguard-go 0.0.20200320 (stesso commit dell'engine iOS) con una TUN a
// canali in memoria e risponde soltanto a:
//   - ICMP echo request verso TunnelIP;
//   - UDP verso TunnelIP:7 (echo).
//
// Tutto il resto viene contato e scartato.
//
// Le chiavi sono effimere e generate a ogni avvio. La configurazione client
// viene scritta (0600) in -client-conf e non viene mai stampata. All'uscita
// il file viene rimosso.
package main

import (
	"bufio"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"flag"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync/atomic"
	"syscall"
	"time"

	"golang.org/x/crypto/curve25519"
	"golang.zx2c4.com/wireguard/device"
	"golang.zx2c4.com/wireguard/tun/tuntest"
)

type keypair struct{ priv, pub []byte }

func newKeypair() keypair {
	priv := make([]byte, 32)
	if _, err := rand.Read(priv); err != nil {
		panic(err)
	}
	priv[0] &= 248
	priv[31] = (priv[31] & 127) | 64
	pub, err := curve25519.X25519(priv, curve25519.Basepoint)
	if err != nil {
		panic(err)
	}
	return keypair{priv, pub}
}

func checksum(b []byte, initial uint32) uint16 {
	sum := initial
	for i := 0; i+1 < len(b); i += 2 {
		sum += uint32(binary.BigEndian.Uint16(b[i:]))
	}
	if len(b)%2 == 1 {
		sum += uint32(b[len(b)-1]) << 8
	}
	for sum > 0xffff {
		sum = (sum >> 16) + (sum & 0xffff)
	}
	return ^uint16(sum)
}

func ipv4Packet(proto byte, src, dst net.IP, payload []byte) []byte {
	p := make([]byte, 20+len(payload))
	p[0] = 0x45
	binary.BigEndian.PutUint16(p[2:], uint16(len(p)))
	p[8] = 64
	p[9] = proto
	copy(p[12:16], src.To4())
	copy(p[16:20], dst.To4())
	binary.BigEndian.PutUint16(p[10:], checksum(p[:20], 0))
	copy(p[20:], payload)
	return p
}

func udpPacket(src, dst net.IP, sport, dport uint16, data []byte) []byte {
	u := make([]byte, 8+len(data))
	binary.BigEndian.PutUint16(u[0:], sport)
	binary.BigEndian.PutUint16(u[2:], dport)
	binary.BigEndian.PutUint16(u[4:], uint16(len(u)))
	copy(u[8:], data)
	// checksum UDP con pseudo-header
	var pseudo uint32
	s4, d4 := src.To4(), dst.To4()
	pseudo += uint32(binary.BigEndian.Uint16(s4[0:])) + uint32(binary.BigEndian.Uint16(s4[2:]))
	pseudo += uint32(binary.BigEndian.Uint16(d4[0:])) + uint32(binary.BigEndian.Uint16(d4[2:]))
	pseudo += 17 + uint32(len(u))
	c := checksum(u, pseudo)
	if c == 0 {
		c = 0xffff
	}
	binary.BigEndian.PutUint16(u[6:], c)
	return ipv4Packet(17, src, dst, u)
}

type counters struct {
	icmpEcho, udpEcho, dnsAnswers, dropped uint64
}

// dnsAnswer risponde a qualsiasi query DNS con NOERROR; per le query A/IN
// aggiunge un record A fisso uguale a answerIP (TTL 5 s).
func dnsAnswer(q []byte, answerIP net.IP) []byte {
	if len(q) < 12 || q[2]&0x80 != 0 || binary.BigEndian.Uint16(q[4:]) != 1 {
		return nil
	}
	i := 12
	for i < len(q) && q[i] != 0 {
		if q[i]&0xc0 != 0 {
			return nil
		}
		i += int(q[i]) + 1
	}
	if i+5 > len(q) {
		return nil
	}
	qend := i + 5
	qtype := binary.BigEndian.Uint16(q[i+1:])
	qclass := binary.BigEndian.Uint16(q[i+3:])
	r := append([]byte(nil), q[:qend]...)
	r[2] = 0x80 | (q[2] & 0x01) // QR + RD copiato
	r[3] = 0x80                 // RA, RCODE=0
	binary.BigEndian.PutUint16(r[6:], 0)
	binary.BigEndian.PutUint16(r[8:], 0)
	binary.BigEndian.PutUint16(r[10:], 0)
	if qtype == 1 && qclass == 1 {
		binary.BigEndian.PutUint16(r[6:], 1)
		r = append(r, 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 5, 0, 4)
		r = append(r, answerIP.To4()...)
	}
	return r
}

// respond restituisce il pacchetto di risposta oppure nil.
func respond(pkt []byte, tunnelIP net.IP, c *counters) []byte {
	if len(pkt) < 20 || pkt[0]>>4 != 4 {
		atomic.AddUint64(&c.dropped, 1)
		return nil
	}
	ihl := int(pkt[0]&0x0f) * 4
	total := int(binary.BigEndian.Uint16(pkt[2:]))
	if ihl < 20 || total > len(pkt) || total < ihl {
		atomic.AddUint64(&c.dropped, 1)
		return nil
	}
	src := net.IP(append([]byte(nil), pkt[12:16]...))
	dst := net.IP(pkt[16:20])
	if !dst.Equal(tunnelIP) {
		atomic.AddUint64(&c.dropped, 1)
		return nil
	}
	body := pkt[ihl:total]
	switch pkt[9] {
	case 1: // ICMP
		if len(body) >= 8 && body[0] == 8 {
			reply := append([]byte(nil), body...)
			reply[0] = 0
			reply[2], reply[3] = 0, 0
			binary.BigEndian.PutUint16(reply[2:], checksum(reply, 0))
			atomic.AddUint64(&c.icmpEcho, 1)
			return ipv4Packet(1, tunnelIP, src, reply)
		}
	case 17: // UDP
		if len(body) >= 8 && binary.BigEndian.Uint16(body[2:]) == 7 {
			sport := binary.BigEndian.Uint16(body[0:])
			ulen := int(binary.BigEndian.Uint16(body[4:]))
			if ulen < 8 || ulen > len(body) {
				break
			}
			atomic.AddUint64(&c.udpEcho, 1)
			return udpPacket(tunnelIP, src, 7, sport, body[8:ulen])
		}
		if len(body) >= 8 && binary.BigEndian.Uint16(body[2:]) == 53 {
			sport := binary.BigEndian.Uint16(body[0:])
			ulen := int(binary.BigEndian.Uint16(body[4:]))
			if ulen < 8 || ulen > len(body) {
				break
			}
			if answer := dnsAnswer(body[8:ulen], tunnelIP); answer != nil {
				atomic.AddUint64(&c.dnsAnswers, 1)
				return udpPacket(tunnelIP, src, 53, sport, answer)
			}
		}
	}
	atomic.AddUint64(&c.dropped, 1)
	return nil
}

func uapiSet(d *device.Device, cfg string) error {
	return d.IpcSetOperation(bufio.NewReader(strings.NewReader(cfg)))
}

type peerStats struct {
	handshake   int64
	tx, rx      uint64
	hasEndpoint bool
}

func uapiStats(d *device.Device) peerStats {
	var sb strings.Builder
	w := bufio.NewWriter(&sb)
	d.IpcGetOperation(w)
	w.Flush()
	var s peerStats
	for _, line := range strings.Split(sb.String(), "\n") {
		kv := strings.SplitN(line, "=", 2)
		if len(kv) != 2 {
			continue
		}
		var n int64
		switch kv[0] {
		case "last_handshake_time_sec":
			fmt.Sscan(kv[1], &n)
			s.handshake = n
		case "tx_bytes":
			fmt.Sscan(kv[1], &n)
			s.tx += uint64(n)
		case "rx_bytes":
			fmt.Sscan(kv[1], &n)
			s.rx += uint64(n)
		case "endpoint":
			s.hasEndpoint = true
		}
	}
	return s
}

func main() {
	listenPort := flag.Int("listen-port", 51830, "porta UDP locale del peer di test")
	endpointHost := flag.String("endpoint-host", "", "IP del PC raggiungibile dall'iPad (obbligatorio)")
	tunnelIPText := flag.String("tunnel-ip", "10.253.92.1", "IP del peer dentro il tunnel")
	clientIPText := flag.String("client-ip", "10.253.92.2", "IP assegnato a NineGuard")
	keepalive := flag.Int("keepalive", 0, "PersistentKeepalive scritto nel profilo client (0 = assente)")
	clientConf := flag.String("client-conf", "", "file 0600 dove scrivere il profilo client")
	duration := flag.Duration("duration", 10*time.Minute, "durata massima")
	selftest := flag.Bool("selftest", false, "verifica locale con un client in-process")
	full := flag.Bool("full", false, "profilo client full tunnel: AllowedIPs 0.0.0.0/0, DNS = tunnel-ip")
	flag.Parse()

	tunnelIP := net.ParseIP(*tunnelIPText).To4()
	clientIP := net.ParseIP(*clientIPText).To4()
	if tunnelIP == nil || clientIP == nil || *listenPort <= 0 || *listenPort > 65535 {
		fmt.Println("STOP: parametri non validi")
		os.Exit(2)
	}
	if *selftest && *endpointHost == "" {
		*endpointHost = "127.0.0.1"
	}
	if net.ParseIP(*endpointHost).To4() == nil {
		fmt.Println("STOP: -endpoint-host deve essere un IPv4 del PC")
		os.Exit(2)
	}
	if *clientConf == "" {
		fmt.Println("STOP: -client-conf obbligatorio")
		os.Exit(2)
	}

	server, client := newKeypair(), newKeypair()
	st := tuntest.NewChannelTUN()
	dev := device.NewDevice(st.TUN(), device.NewLogger(device.LogLevelError, "TESTPEER: "))
	defer dev.Close()
	cfg := fmt.Sprintf("private_key=%s\nlisten_port=%d\nreplace_peers=true\n"+
		"public_key=%s\nreplace_allowed_ips=true\nallowed_ip=%s/32\n\n",
		hex.EncodeToString(server.priv), *listenPort,
		hex.EncodeToString(client.pub), clientIP)
	if err := uapiSet(dev, cfg); err != nil {
		fmt.Println("STOP: configurazione UAPI del peer fallita")
		os.Exit(1)
	}

	allowed, dnsLine := tunnelIP.String()+"/32", ""
	if *full {
		allowed, dnsLine = "0.0.0.0/0", fmt.Sprintf("DNS = %s\n", tunnelIP)
	}
	conf := fmt.Sprintf("[Interface]\nPrivateKey = %s\nAddress = %s/32\n%s"+
		"[Peer]\nPublicKey = %s\nAllowedIPs = %s\nEndpoint = %s:%d\n",
		base64.StdEncoding.EncodeToString(client.priv), clientIP, dnsLine,
		base64.StdEncoding.EncodeToString(server.pub), allowed,
		*endpointHost, *listenPort)
	if *keepalive > 0 {
		conf += fmt.Sprintf("PersistentKeepalive = %d\n", *keepalive)
	}
	os.Remove(*clientConf)
	if err := ioutil.WriteFile(*clientConf, []byte(conf), 0600); err != nil {
		fmt.Println("STOP: impossibile scrivere il profilo client")
		os.Exit(1)
	}
	defer os.Remove(*clientConf)
	fmt.Printf("TESTPEER_READY=YES PORT=%d TUNNEL_IP=%s CLIENT_IP=%s FULL=%v\n",
		*listenPort, tunnelIP, clientIP, *full)

	var c counters
	go func() {
		for pkt := range st.Inbound {
			if reply := respond(pkt, tunnelIP, &c); reply != nil {
				st.Outbound <- reply
			}
		}
	}()

	if *selftest {
		code := runSelftest(conf, client, server, *listenPort, tunnelIP, clientIP, dev, &c)
		os.Remove(*clientConf)
		dev.Close()
		os.Exit(code)
	}

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()
	deadline := time.After(*duration)
	for {
		select {
		case <-ticker.C:
			s := uapiStats(dev)
			age := "NEVER"
			if s.handshake > 0 {
				age = fmt.Sprint(time.Now().Unix() - s.handshake)
			}
			fmt.Printf("TESTPEER_STATS HANDSHAKE_AGE=%s TX=%d RX=%d ICMP_ECHO=%d UDP_ECHO=%d DNS=%d DROPPED=%d\n",
				age, s.tx, s.rx, atomic.LoadUint64(&c.icmpEcho),
				atomic.LoadUint64(&c.udpEcho), atomic.LoadUint64(&c.dnsAnswers),
				atomic.LoadUint64(&c.dropped))
		case <-stop:
			fmt.Println("TESTPEER_EXIT=SIGNAL")
			return
		case <-deadline:
			fmt.Println("TESTPEER_EXIT=TIMEOUT")
			return
		}
	}
}

// runSelftest avvia un client in-process con il profilo generato e verifica
// handshake ed echo UDP su UDP reale in loopback.
func runSelftest(conf string, client, server keypair, port int, tunnelIP, clientIP net.IP,
	srv *device.Device, c *counters) int {
	ct := tuntest.NewChannelTUN()
	cdev := device.NewDevice(ct.TUN(), device.NewLogger(device.LogLevelError, "CLIENT: "))
	defer cdev.Close()
	cfg := fmt.Sprintf("private_key=%s\nreplace_peers=true\npublic_key=%s\n"+
		"endpoint=127.0.0.1:%d\nreplace_allowed_ips=true\nallowed_ip=%s/32\n\n",
		hex.EncodeToString(client.priv), hex.EncodeToString(server.pub), port, tunnelIP)
	if err := uapiSet(cdev, cfg); err != nil {
		fmt.Println("SELFTEST=FAILED CLIENT_UAPI")
		return 1
	}
	if !strings.Contains(conf, "Endpoint = 127.0.0.1:") {
		fmt.Println("SELFTEST=FAILED CONF")
		return 1
	}
	ct.Outbound <- udpPacket(clientIP, tunnelIP, 40000, 7, []byte("NINEGUARD3"))
	select {
	case reply := <-ct.Inbound:
		ok := len(reply) >= 28 && string(reply[28:]) == "NINEGUARD3" &&
			net.IP(reply[12:16]).Equal(tunnelIP) && net.IP(reply[16:20]).Equal(clientIP) &&
			binary.BigEndian.Uint16(reply[20:]) == 7 && binary.BigEndian.Uint16(reply[22:]) == 40000
		s := uapiStats(srv)
		fmt.Printf("SELFTEST_UDP_ECHO=%v SERVER_HANDSHAKE=%v SERVER_RX=%d SERVER_TX=%d\n",
			ok, s.handshake > 0, s.rx, s.tx)
		if ok && s.handshake > 0 && s.rx > 0 && s.tx > 0 {
			fmt.Println("SELFTEST=OK")
			return 0
		}
	case <-time.After(5 * time.Second):
		fmt.Println("SELFTEST=FAILED TIMEOUT")
		return 1
	}
	fmt.Println("SELFTEST=FAILED")
	return 1
}
