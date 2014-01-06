#include "structures.h"
#include "udp.h"
#include "ts_util.h"

#include <stdio.h>
#include <string.h>
#include <pcap.h>
#include <unistd.h>

int GetDefaultDevice(char** res) {
    char errbuf[PCAP_ERRBUF_SIZE];
    char* dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\nMay be is it need to be root?\n", errbuf);
        return 1;
    }
    *res = dev;
    return 0;
}

int PrintDefaultDevice() {
    char* dev;
    int res = GetDefaultDevice(&dev);
    if (res)
        return res;
    printf("Default device:\n%s\n\n", dev);
    return 0;
}

int PrintAllDevices() {
    int err = PrintDefaultDevice();
    if (err)
        return err;

    struct pcap_if* found_devices;
    int result;
    char errbuf[PCAP_ERRBUF_SIZE];
    errbuf[0] = 0;
    result = pcap_findalldevs(&found_devices, errbuf);
    if (result < 0) {
        fprintf(stderr, "Device scan error:\n%s\n", errbuf);
        return 1;
    }
    if (errbuf[0]) {
        fprintf(stderr, "Device scan warning:\n%s\n", errbuf);
    }

    printf("All devices:\n");
    struct pcap_if* iter = found_devices;
    while (iter != NULL) {
        printf("%s\n", iter->name);
        iter = iter->next;
    }
    pcap_freealldevs(found_devices);
    return 0;
}

void PrintHelp(char* progName) {
    fprintf(stderr,
        "USAGE: %s [-lh]\n"
        "\n"
        "-l    print list of suitable devices. Must be root\n"
        "-h    print this help\n"
        , progName
    );
}

pcap_t* pcap;
pcap_dumper_t* dumper;
int offline;
struct timespec starttime;
struct timespec timenow;

int Init(char* name) {
    clock_gettime(CLOCK_REALTIME, &starttime);
    timenow = starttime;
    if (name[0] == '-' && name[1] == 0) {
        offline = 1;
        pcap = pcap_open_dead(DLT_EN10MB, 65535);
        dumper = pcap_dump_open(pcap, name);
    } else {
        offline = 0;
        if (strcmp(name, "default") == 0) {
            char* dev;
            int res = GetDefaultDevice(&dev);
            if (res)
                return res;
            fprintf(stderr, "Using default device: %s\n", dev);
            name = dev;
        }
        char pcap_errbuff[PCAP_ERRBUF_SIZE];
        pcap_errbuff[0] = 0;
        pcap =  pcap_open_live(name, BUFSIZ, 0, 0, pcap_errbuff);
        if (pcap_errbuff[0]) {
            fprintf(stderr, "%s\n", pcap_errbuff);
        }
        if (!pcap)
            return 1;
    }
    return 0;
}

int Finish() {
    if (offline) {
        pcap_dump_close(dumper);
        pcap_close(pcap);
    } else {
        pcap_close(pcap);
    }
}

void WaitFor(struct timeval ts) {
    struct timespec sendtime;
    TimevalToTimespec(&ts, &sendtime);
    sendtime = TsAdd(sendtime, starttime);
    if (TsCompare(sendtime,timenow) <= 0)
        return;
    clock_gettime(CLOCK_REALTIME, &timenow);
    if (TsCompare(sendtime, timenow) <= 0)
        return;
    struct timespec waittime;
    waittime = TsSubtract(sendtime, timenow);
    if (nanosleep(&waittime, NULL)) {
        fprintf(stderr, "nanosleep error\n");
    }
}

int SendPacket(struct TUDPPacket* packet, struct timeval ts) {
    if (offline) {
        struct pcap_pkthdr header;
        header.ts = ts;
        header.caplen = packet->Size;
        header.len = packet->Size;
        pcap_dump((u_char* ) dumper, &header, (u_char *) &packet->Ethernet);
    } else {
        WaitFor(ts);
        if (pcap_inject(pcap, (u_char *) &packet->Ethernet, packet->Size) == -1) {
            pcap_perror(pcap, 0);
            pcap_close(pcap);
            return 1;
        }
    }
    return 0;
}


int SendTestTraffic(char* device) {
    struct TUDPPacket packet;
    InitUDPPacket(&packet);
    const int DATASIZE = 1450;
    char data[DATASIZE];
    memset(data, 'x', sizeof(data));
    SetData(&packet, data, sizeof(data));
    int i;
    int res = Init("default");
    if (res)
        return res;
    struct timeval ts;
    ts.tv_sec = 0; // seconds from start
    ts.tv_usec = 0; // microseconds
    for (i = 0; i < 1000000; ++i) {
        SetPort(&packet, 10000 + i%100, 20000 + i%100);
        res = SendPacket(&packet, ts);
        if (res)
            return res;
    }
    Finish();
    return 0;
}

uint8_t ReverseBits(uint8_t x) {
    uint8_t res = 0;
    int i = 0;
    for (i = 0; i < 8; ++i) {
        if (x & (1 << i))
            res |= 1 << (7 - i);
    }
    return res;
}

void WritePacketNum(char* dest, uint32_t packetNum) {
    int i;
    for (i = 0; i < 4; ++i) {
        dest[i] = packetNum & 255;
        packetNum >>= 8;
    }
}

void WriteReversed(char* dest, uint32_t data, int cnt) {
    int i;
    for (i = 0; i < cnt; ++i) {
        dest[i] = ReverseBits((uint8_t)(data & 255));
        data >>= 8;
    }
}

int ManyNetworksTest() {
    Init("-");
    int packetsPerTest = 10;
    int start = 1;
    int step = 3;
    int tests = 10;
    int testNum;
    uint32_t networkCount = start;
    uint32_t packetNum = 0;
    struct TUDPPacket packet;
    InitUDPPacket(&packet);
    uint8_t payload[18];
    memset(payload, 'x', sizeof(payload));
    uint8_t sourceIP[4];
    uint8_t destIP[4];
    sourceIP[0] = 200;
    destIP[0] = 201;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    for (testNum = 0; testNum < tests; ++testNum) {
        networkCount = start + step*testNum;
        if (networkCount == 0)
            networkCount = 1;
        int i;
        for (i = 0; i < packetsPerTest; ++i) {
            WritePacketNum(payload, packetNum);
            WriteReversed(sourceIP + 1, i % networkCount, 3);
            WriteReversed(destIP + 1, i % networkCount, 3);
            SetIP(&packet, sourceIP, destIP);
            SetData(&packet, payload, sizeof(payload));
            SendPacket(&packet, tv);
            ++packetNum;
        }
    }
    Finish();
}

void DifferentPayloadSizeTest() {
    Init("-");
    int packetsPerTest = 10;
    int start = 18;
    int step = 1;
    int tests = MXUDP - start + 1;
    int testNum;
    uint32_t packetNum = 0;
    struct TUDPPacket packet;
    InitUDPPacket(&packet);
    uint8_t payload[MXUDP];
    memset(payload, 'x', sizeof(payload));
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    for (testNum = 0; testNum < tests; ++testNum) {
        int size = start + testNum*step;
        if (size < 18)
            size = 18;
        if (size > MXUDP)
            size = MXUDP;
        int i;
        for (i = 0; i < packetsPerTest; ++i) {
            WritePacketNum(payload, packetNum);
            SetData(&packet, payload, size);
            SendPacket(&packet, tv);
            ++packetNum;
        }
    }
    Finish();
}

void LowTTLTest() {
    Init("-");
    int packetsPerTest = 10;
    double start = 0;
    double step = 0.1;
    int tests = 10;
    int testNum;
    uint32_t packetNum = 0;
    struct TUDPPacket packet;
    InitUDPPacket(&packet);
    uint8_t payload[18];
    memset(payload, 'x', sizeof(payload));
    struct timeval tv;
    tv.tv_sec = 0;
    for (testNum = 0; testNum < tests; ++testNum) {
        double frenq = start + testNum*step;
        int badPackets = 0;
        int i;
        for (i = 0; i < packetsPerTest; ++i) {
            WritePacketNum(payload, packetNum);
            SetData(&packet, payload, sizeof(payload));
            if (i == 0 || i == packetsPerTest - 1 || badPackets < i*frenq) {
                SetTTL(&packet, 1);
            } else {
                SetTTL(&packet, 64);
            }
            SendPacket(&packet, tv);
        }
    }
    Finish();
}

void BadMacTest() {
    Init("-");
    int packetsPerTest = 10;
    double start = 0;
    double step = 0.1;
    int tests = 10;
    int testNum;
    uint32_t packetNum = 0;
    struct TUDPPacket packet;
    InitUDPPacket(&packet);
    uint8_t payload[18];
    memset(payload, 'x', sizeof(payload));
    struct timeval tv;
    tv.tv_sec = 0;
    for (testNum = 0; testNum < tests; ++testNum) {
        double frenq = start + testNum*step;
        int badPackets = 0;
        int i;
        for (i = 0; i < packetsPerTest; ++i) {
            WritePacketNum(payload, packetNum);
            SetData(&packet, payload, sizeof(payload));
            if (i == 0 || i == packetsPerTest - 1 || badPackets < i*frenq) {
                SetMac(&packet, SourceMac, DestMac);
            } else {
                SetMac(&packet, SourceMac, FakeDestMac);
            }
            SendPacket(&packet, tv);
        }
    }
    Finish();
}

int main(int argc, char *argv[])
{
    DifferentPayloadSizeTest();
    return 0;
    int c;
    int hasArgs = 0;
    char* device = NULL;
    while((c = getopt(argc, argv, "lhd:t")) != -1) {
        hasArgs = 1;
        switch (c)
        {
        case 'l':
            return PrintAllDevices();
        case 'h':
            PrintHelp(argv[0]);
            return 1;
        case 'd':
            device = optarg;
            break;
        }
    }
    if (!hasArgs) {
        PrintHelp(argv[0]);
        return 1;
    }
    return 0;
}
