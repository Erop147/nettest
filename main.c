#include <stdio.h>
#include <string.h>
#include <pcap.h>
#include <unistd.h>


int PrintExistingDevices() {
    struct pcap_if* found_devices;
    int result;
    char errbuf[PCAP_ERRBUF_SIZE];
    errbuf[0] = 0;
    result = pcap_findalldevs(&found_devices, errbuf);
    if (result < 0 || strlen(errbuf) > 0) {
        printf("Device scan error:\n%s\nMay be it need to be root?\n",errbuf);
        return -1;
    }

    while(found_devices != NULL) {
        printf("%s\n",found_devices->name);
        found_devices = found_devices->next;
    }
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

int main(int argc, char *argv[])
{
    int c;
    int hasArgs = 0;
    while((c = getopt(argc, argv, "lh")) != -1) {
        hasArgs = 1;
        switch (c)
        {
        case 'l':
            return PrintExistingDevices();
        case 'h':
            PrintHelp(argv[0]);
            return 1;
        }
    }
    if (!hasArgs) {
        PrintHelp(argv[0]);
        return 1;
    }
    return 0;
    char *dev, errbuf[PCAP_ERRBUF_SIZE];

    dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return(2);
    }
    printf("Device: %s\n", dev);
    return 0;
}