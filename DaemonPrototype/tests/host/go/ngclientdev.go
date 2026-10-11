package main

import (
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"time"

	"golang.zx2c4.com/wireguard/device"
	"golang.zx2c4.com/wireguard/tun/tuntest"
)

func csum(b []byte) uint16 {
	var s uint32
	for i := 0; i+1 < len(b); i += 2 {
		s += uint32(binary.BigEndian.Uint16(b[i:]))
	}
	for s > 0xffff {
		s = s>>16 + s&0xffff
	}
	return ^uint16(s)
}

func main() {
	sock, trigger := os.Args[1], os.Args[2]
	t := tuntest.NewChannelTUN()
	d := device.NewDevice(t.TUN(), device.NewLogger(device.LogLevelError, "CLIENTDEV: "))
	os.Remove(sock)
	l, _ := net.Listen("unix", sock)
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go d.IpcHandle(c)
		}
	}()
	for {
		if _, err := os.Stat(trigger); err == nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	data := []byte("NINEGUARD3")
	p := make([]byte, 28+len(data))
	p[0] = 0x45
	binary.BigEndian.PutUint16(p[2:], uint16(len(p)))
	p[8] = 64
	p[9] = 17
	copy(p[12:], net.ParseIP("10.253.92.2").To4())
	copy(p[16:], net.ParseIP("10.253.92.1").To4())
	binary.BigEndian.PutUint16(p[10:], csum(p[:20]))
	binary.BigEndian.PutUint16(p[20:], 40001)
	binary.BigEndian.PutUint16(p[22:], 7)
	binary.BigEndian.PutUint16(p[24:], uint16(8+len(data)))
	copy(p[28:], data)
	t.Outbound <- p
	select {
	case r := <-t.Inbound:
		fmt.Println("CLIENTDEV_ECHO=", len(r) >= 28 && string(r[28:]) == "NINEGUARD3")
	case <-time.After(5 * time.Second):
		fmt.Println("CLIENTDEV_ECHO= false timeout")
	}
	time.Sleep(2 * time.Second)
	d.Close()
}
