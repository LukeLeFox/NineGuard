package main

import (
	"bufio"
	"bytes"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"strings"
	"time"

	"golang.org/x/crypto/curve25519"
	"golang.zx2c4.com/wireguard/device"
	"golang.zx2c4.com/wireguard/tun/tuntest"
)

func keypair() ([]byte, []byte) {
	priv := make([]byte, 32)
	rand.Read(priv)
	priv[0] &= 248
	priv[31] = (priv[31] & 127) | 64
	pub, _ := curve25519.X25519(priv, curve25519.Basepoint)
	return priv, pub
}

func serve(path string, d *device.Device) {
	os.Remove(path)
	l, err := net.Listen("unix", path)
	if err != nil {
		panic(err)
	}
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go d.IpcHandle(c)
		}
	}()
}

func main() {
	sockA, sockB, port, info, trigger := os.Args[1], os.Args[2], os.Args[3], os.Args[4], os.Args[5]
	privA, pubA := keypair()
	privB, pubB := keypair()
	tA, tB := tuntest.NewChannelTUN(), tuntest.NewChannelTUN()
	dA := device.NewDevice(tA.TUN(), device.NewLogger(device.LogLevelError, "A: "))
	dB := device.NewDevice(tB.TUN(), device.NewLogger(device.LogLevelError, "B: "))
	cfgB := fmt.Sprintf("private_key=%s\nlisten_port=%s\npublic_key=%s\nallowed_ip=10.253.91.2/32\n\n",
		hex.EncodeToString(privB), port, hex.EncodeToString(pubA))
	if err := dB.IpcSetOperation(bufio.NewReader(strings.NewReader(cfgB))); err != nil {
		panic(err)
	}
	serve(sockA, dA)
	serve(sockB, dB)
	os.WriteFile(info, []byte(fmt.Sprintf("%s\n%s\n", base64.StdEncoding.EncodeToString(privA), base64.StdEncoding.EncodeToString(pubB))), 0600)
	for {
		if _, err := os.Stat(trigger); err == nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	ping := tuntest.Ping(net.ParseIP("10.253.91.1"), net.ParseIP("10.253.91.2"))
	tA.Outbound <- ping
	select {
	case got := <-tB.Inbound:
		fmt.Println("HARNESS_DELIVERED=", bytes.Equal(got, ping))
	case <-time.After(5 * time.Second):
		fmt.Println("HARNESS_DELIVERED= false (timeout)")
	}
	time.Sleep(3 * time.Second) // lascia al driver il tempo di leggere le statistiche
	dA.Close()
	dB.Close()
}
