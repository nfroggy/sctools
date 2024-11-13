#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


FILE *logfile;
#define ERR_EXIT(...) do { printf(__VA_ARGS__); fprintf(logfile, __VA_ARGS__); abort(); } while(0)

FILE *pMap;

#define DATA_SIZE 246
#define PATH_LEN 36
#pragma pack(push, 1)
struct PMS {
    uint32_t PMS_number;
    char FileInName[10][PATH_LEN];
    uint16_t PAddress[10];
    uint16_t FileId[10];
    uint16_t RAddress[10];
    uint16_t HeaderOffset[10];
    uint16_t GameTimeWord[10];
    char ServiceID[10];
    char PMR_fill[38];
};
#pragma pack(pop)

struct PMS PacketMapStruct;

void Setup(int *maxPackets, int *maxFile, char *outname) {
    FILE *fp = fopen("parm.dat", "r");
    if (!fp) {
        ERR_EXIT("sf error - opening parameters\n");
    }

    if (fscanf(fp, "%d %d %s\n", maxPackets, maxFile, outname) != 3) {
        ERR_EXIT("sf error - reading parameters\n");
    }

    fprintf(logfile, "MaxPackets=%d MaxFile=%d Outname=%s\n", *maxPackets, *maxFile, outname);
}

void GetData(int pipeNum, int packetNum, uint16_t *pAddress, uint8_t *data, uint16_t *fileId, uint16_t *rAddress, uint16_t *gameTimeWord, uint8_t *serviceId) {
    long seekaddress;
    char fileInName[PATH_LEN];

    if ((packetNum == 0) && (pipeNum == 0)) {
        pMap = fopen("pmap.dat", "rb");
        if (!pMap) {
            ERR_EXIT("sf error - getdata - pmap not opened\n");
        }
    }

    seekaddress = packetNum * 512;
    if (pipeNum == 0) {
        fread(&PacketMapStruct, sizeof(PacketMapStruct), 1, pMap);
    }
    if (PacketMapStruct.PMS_number != seekaddress) {
        printf("sf error - getdata - PMAP read address invalid - on %d\n", PacketMapStruct.PMS_number);
        printf("packet=%d seekaddress=%d recsize=%zu\n", packetNum, seekaddress, sizeof(PacketMapStruct));
        fprintf(logfile, "sf error - getdata - PMAP read address invalid - on %d\n", PacketMapStruct.PMS_number);
        fclose(logfile);
        abort();
    }
    if ((pipeNum < 0) || (pipeNum > 9)) {
        ERR_EXIT("sf error - getdata - incorrect mux for PMAP\n");
    }

    *pAddress = PacketMapStruct.PAddress[pipeNum];
    *rAddress = PacketMapStruct.RAddress[pipeNum];
    *fileId = PacketMapStruct.FileId[pipeNum];
    *gameTimeWord = PacketMapStruct.GameTimeWord[pipeNum];
    *serviceId = PacketMapStruct.ServiceID[pipeNum];
    sscanf(PacketMapStruct.FileInName[pipeNum], "%s\n", fileInName);
    if (fileInName[0] != '*') {
        FILE *dataFile = fopen(fileInName, "rb");
        if (!dataFile) {
            ERR_EXIT("sf error - opening game data - %s\n", fileInName);
        }

        long seekpack = (*pAddress * DATA_SIZE) + PacketMapStruct.HeaderOffset[pipeNum];
        if (fseek(dataFile, seekpack, SEEK_SET)) {
            ERR_EXIT("sf error - GetData - on DataSeek - %s\n", fileInName);
        }
        if (!fread(data, 1, DATA_SIZE, dataFile)) {
            ERR_EXIT("sf error - getdata - on read\n");
        }
        fclose(dataFile);
    }
    else {
        *pAddress = 0;
        *rAddress = 0;
        *fileId = 0x3fff;
        for (int i = 0; i < DATA_SIZE; i += 2) {
            data[i] = 0;
            data[i + 1] = 1;
        }
    }
}

void RevBitsInByte(uint8_t *source, int startBit, int stopBit) {
    uint8_t smask;
    uint8_t dmask;
    uint8_t map[9];
    uint8_t rmap[9];
    int reverseBit;

    smask = 1;
    for (int i = 1; i <= 8; i++) {
        if (*source & smask) {
            map[i] = 1;
        }
        else {
            map[i] = 0;
        }
        smask <<= 1;
    }

    reverseBit = stopBit;
    for (int i = startBit; i <= stopBit; i++) {
        rmap[i] = map[reverseBit];
        reverseBit--;
    }

    dmask = 0;
    for (int i = 1; i <= 8; i++) {
        uint8_t *currMap = ((i >= startBit) && (i <= stopBit)) ? rmap : map;
        if (currMap[i]) {
            dmask |= (1 << (i - 1));
        }
    }
    *source = dmask;
}

void RevBitsInWord(uint16_t *source, int startBit, int stopBit) {
    uint16_t smask;
    uint16_t dmask;
    uint8_t map[17];
    uint8_t rmap[17];
    int reverseBit;

    smask = 1;
    for (int i = 1; i <= 16; i++) {
        if (*source & smask) {
            map[i] = 1;
        }
        else {
            map[i] = 0;
        }
        smask <<= 1;
    }

    reverseBit = stopBit;
    for (int i = startBit; i <= stopBit; i++) {
        rmap[i] = map[reverseBit];
        reverseBit--;
    }

    dmask = 0;
    for (int i = 1; i <= 16; i++) {
        uint8_t *currMap = ((i >= startBit) && (i <= stopBit)) ? rmap : map;
        if (currMap[i]) {
            dmask |= (1 << (i - 1));
        }
    }
    *source = dmask;
}

void OrBits(uint8_t *source, int sbitoff, uint8_t *destin, int dbitoff, int numbit) {
    div_t bitsAndBytes;
    uint8_t smask, dmask;

    int sbytes = 0;
    if (sbitoff > 8) {
        bitsAndBytes = div(sbitoff - 1, 8);
        sbytes = bitsAndBytes.quot;
        sbitoff = bitsAndBytes.rem + 1;
    }

    int dbytes = 0;
    if (dbitoff > 8) {
        bitsAndBytes = div(dbitoff - 1, 8);
        dbytes = bitsAndBytes.quot;
        dbitoff = bitsAndBytes.rem + 1;
    }

    while (numbit) {
        if (sbitoff == 9) {
            sbytes++;
            sbitoff = 1;
        }
        if (dbitoff == 9) {
            dbytes++;
            dbitoff = 1;
        }
        smask = (source[sbytes] << (8 - sbitoff)) & 0x80;
        dmask = 1 << (dbitoff - 1);
        if (smask) {
            destin[dbytes] |= dmask;
        }
        dbitoff++;
        sbitoff++;
        numbit--;
    }
}

uint16_t CalcCRC(uint8_t *source, int startBit, int stopBit) {
    div_t bitsAndBytes;
    int bytePos = 0;
    uint16_t crc = 0;

    if (startBit > 8) {
        bitsAndBytes = div(startBit - 1, 8);
        bytePos = bitsAndBytes.quot;
        startBit = bitsAndBytes.rem + 1;
    }

    uint8_t *data = source + bytePos;
    uint8_t srcMask = 1 << (startBit - 1);
    for (int i = 1; i <= stopBit; i++) {
        fprintf(logfile, (*data & srcMask) ? "1" : "0");
        
        int a = !!(crc & 0x8000);
        int b = !!(*data & srcMask);
        a ^= b;
        if (a) {
            crc = ((crc ^ 0x810) << 1) | 1;
        }
        else {
            crc <<= 1;
        }
        if (srcMask == 0x80) {
            srcMask = 1;
            data++;
        }
        else {
            srcMask <<= 1;
        }
    }
    return ~crc;
}

uint16_t CalcBCH(uint8_t *source, int sbitoff, int numbit) {
    div_t bitsAndBytes;
    int bytePos = 0;
    uint16_t bch = 0;

    if (sbitoff > 8) {
        bitsAndBytes = div(sbitoff - 1, 8);
        bytePos = bitsAndBytes.quot;
        sbitoff = bitsAndBytes.rem + 1;
    }

    uint8_t *data = source + bytePos;
    uint8_t srcMask = 1 << (sbitoff - 1);

    for (int i = 1; i <= numbit; i++) {
        int a = !!(*data & srcMask);
        int b = !!(bch & 0x8000);
        a ^= b;
        if (a) {
            bch = ((bch ^ 0x37b1) << 1) | 1;
        }
        else {
            bch <<= 1;
        }
        if (srcMask == 0x80) {
            srcMask = 1;
            data++;
        }
        else {
            srcMask <<= 1;
        }
    }
    return bch;
}

uint8_t CalcParity(uint8_t *source, int sbitoff, int numbit) {
    div_t bitsAndBytes;
    int bytePos = 0;
    uint8_t parity = 0;

    if (sbitoff > 8) {
        bitsAndBytes = div(sbitoff - 1, 8);
        bytePos = bitsAndBytes.quot;
        sbitoff = bitsAndBytes.rem + 1;
    }

    uint8_t *data = source + bytePos;
    uint8_t srcMask = 1 << (sbitoff - 1);

    for (int i = 1; i <= numbit; i++) {
        parity ^= !!(*data & srcMask);
        if (srcMask == 0x80) {
            srcMask = 1;
            data++;
        }
        else {
            srcMask <<= 1;
        }
    }
    return parity;
}

void LoadFrame(int pipeNum, uint16_t pAddress, uint16_t rAddress, uint16_t fileID, uint8_t *frame, uint8_t *data, uint16_t gameTimeWord, uint8_t serviceID) {
    uint8_t rData[DATA_SIZE];

    for (int i = 0; i < DATA_SIZE; i++) {
        rData[i] = data[i];
        RevBitsInByte(rData + i, 1, 8);
    }

    // --- header ---
    int bitoff = 28 + 2;
    uint8_t gameTimeSelect = 0xf - (pAddress & 0xf);
    uint8_t gameTimeBit = (PacketMapStruct.GameTimeWord[pipeNum] & (1 << gameTimeSelect)) >> gameTimeSelect;
    uint8_t gameTimeSync = !gameTimeSelect;
    OrBits(&gameTimeSync, 1, frame + (pipeNum * 288), bitoff++, 1);
    OrBits(&gameTimeBit, 1, frame + (pipeNum * 288), bitoff++, 1);
    RevBitsInByte(&serviceID, 1, 7);
    OrBits(&serviceID, 1, frame + (pipeNum * 288), bitoff, 7);
    bitoff += 7;
    RevBitsInWord(&fileID, 1, 14);
    OrBits((uint8_t *)&fileID, 1, frame + (pipeNum * 288), bitoff, 14);
    bitoff += 14;
    pAddress += rAddress;
    RevBitsInWord(&pAddress, 1, 15);
    OrBits((uint8_t *)&pAddress, 1, frame + (pipeNum * 288), bitoff, 15);
    bitoff += 15;
    uint16_t headerCRC = CalcCRC(frame + (pipeNum * 288), 28, 40);
    RevBitsInWord(&headerCRC, 1, 16);
    OrBits((uint8_t *)&headerCRC, 1, frame + (pipeNum * 288), bitoff, 16);
    bitoff += 16;
    OrBits(frame + (pipeNum * 288), 28, frame + (pipeNum * 288) + 10, 4, 56);
    bitoff += 56;

    // --- data ---
    bitoff = 140;
    for (int i = 0; i < DATA_SIZE;) {
        // ???
        if (bitoff == 1153) {
            bitoff += 27;
        }

        uint16_t bch;
        uint8_t parity;
        if (i > 0) {
            OrBits(rData + i, 1, frame + (pipeNum * 288), bitoff, 208);
            bch = CalcBCH(frame + (pipeNum * 288), bitoff, 208);
            parity = CalcParity(frame + (pipeNum * 288), bitoff, 208);
            bitoff += 208;
            i += 26;
        }
        else {
            OrBits(rData, 1, frame + (pipeNum * 288), bitoff, 96);
            bch = CalcBCH(frame + (pipeNum * 288), 28, 208);
            parity = CalcParity(frame + (pipeNum * 288), bitoff, 208);
            bitoff += 96;
            i += 12;
        }

        RevBitsInWord(&bch, 1, 16);
        OrBits((uint8_t *)&bch, 1, frame + (pipeNum * 288), bitoff, 16);
        bitoff += 16;
        OrBits(&parity, 1, frame + (pipeNum * 288), bitoff++, 1);
    }
}

void InterLeave(int pipeNum, uint8_t *frame) {
    uint8_t interFrame[288] = { 0 };
    int packetAoff;
    int packetBoff;
    int packetXoff;
    int packetAbit;
    int packetBbit;
    int bitCount;
    int pc;
    int packet;

    OrBits(frame + (pipeNum * 288), 1, interFrame, 1, 27);
    packetAoff = 1;
    packetBoff = 226;
    packetXoff = packetAoff;
    packet = 0;
    while (packet < 9) {
        if (packet == 0) {
            packetAoff += 27;
            packetBoff += 27;
            packetXoff += 27;
        }
        if (packet == 4) {
            packetBoff += 27;
        }
        if (packet == 6) {
            packetAoff += 27;
            packetXoff += 27;
        }
        
        bitCount = 0;
        pc = 0;
        int *bitPtr = &packetAbit;
        packetAbit = 0;
        packetBbit = 0;
        while (bitCount < 450) {
            if (pc == 2) {
                pc = 0;
                if (bitPtr == &packetAbit) { bitPtr = &packetBbit; }
                else { bitPtr = &packetAbit; }
            }

            if (bitCount == 225) {
                if ((bitPtr != &packetAbit) || (packetAbit != 113)) {
                    printf("interleave error in packet A to B\n");
                    fprintf(logfile, "interleave error in packet A to B\n");
                    fprintf(logfile, "BitCount=%d ", bitCount);
                    if (bitPtr == &packetAbit) {
                        fprintf(logfile, " source= A %d\n", packetAbit);
                    }
                    else {
                        fprintf(logfile, " source= B %d\n", packetBbit);
                    }
                    abort();
                }
                bitPtr = &packetBbit;
                pc = 0;
            }

            int packetBits;
            if (bitPtr == &packetAbit) {
                packetBits = *bitPtr + packetAoff;
            }
            else {
                packetBits = *bitPtr + packetBoff;
            }

            if ((packet == 4) && (bitCount == 225)) {
                packetXoff += 27;
            }

            OrBits(frame + (pipeNum * 288), packetBits, interFrame, bitCount + packetXoff, 1);
            (*bitPtr)++;
            bitCount++;
            pc++;
        }

        packetAoff += 450;
        packetBoff += 450;
        packetXoff = packetAoff;
        packet += 2;
    }

    for (int i = 0; i < 288; i++) {
        frame[(pipeNum * 288) + i] = interFrame[i];
    }
}

void SaveFrame(uint8_t *frame, char *path, int packetNum, int maxPackets) {
    static FILE *outfile;

    if (packetNum == 0) {
        outfile = fopen(path, "wb");
        if (!outfile) {
            ERR_EXIT("sf error - creating outfile\n");
        }
    }
    for (int i = 0; i < 288; i += 2) {
        for (int pipeNum = 0; pipeNum < 10; pipeNum++) {
            if (((pipeNum * 288) + i) == 2592) {
                fprintf(logfile, "\nloaded %d %d @ %x %x\n", pipeNum, i, frame[2592], frame[2593]);
            }
            fputc(frame[(pipeNum * 288) + i], outfile);
            fputc(frame[(pipeNum * 288) + i + 1], outfile);
        }
    }
    if (packetNum == maxPackets) {
        fclose(outfile);
    }
}

int main(int argc, char **argv) {
    uint8_t frame[2880];
    uint8_t data[DATA_SIZE];
    char path[PATH_LEN];
    uint8_t serviceID;
    uint16_t gameTimeWord;
    uint16_t fileID;
    uint16_t rAddress;
    uint16_t pAddress;
    int maxFile;
    int maxPackets;

    logfile = fopen("sf.log", "w");

    Setup(&maxPackets, &maxFile, path);
    printf("\nFormatting Frame\n");

    // loop per packet
    for (int packetNum = 0; packetNum < maxPackets; packetNum++) {
        printf("%5d\b\b\b\b\b\b", packetNum);
        memset(frame, 0, sizeof(frame));
        // loop per pipe
        for (int pipeNum = 0; pipeNum < 10; pipeNum++) {
            GetData(pipeNum, packetNum, &pAddress, data, &fileID, &rAddress, &gameTimeWord, &serviceID);
            LoadFrame(pipeNum, pAddress, rAddress, fileID, frame, data, gameTimeWord, serviceID);
            InterLeave(pipeNum, frame);
        }
        SaveFrame(frame, path, packetNum, maxPackets);
    }

    fclose(logfile);
    return 0;
}
