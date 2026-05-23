#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <direct.h>
#include <io.h>             // for _findfirst / _findnext
#include <sys/stat.h>       // for _mkdir

#define MAX_CHAR 256

typedef struct HuffNode {
    unsigned char ch;//字符 
    int freq;//出现频率 
    struct HuffNode *left, *right;//左右孩子 
} HuffNode;

typedef struct {
    char code[256];//编码字符串 
    int len;//编码长度 
} Code;

// 创建 Huffman 节点
HuffNode* createNode(unsigned char ch, int freq) {
    HuffNode *node = (HuffNode*)malloc(sizeof(HuffNode));
    node->ch = ch;
    node->freq = freq;
    node->left = node->right = NULL;
    return node;
}

// 统计频率
void countFreq(FILE *fp, int freq[]) {
    for (int i = 0; i < 256; i++) freq[i] = 0;
    unsigned char c;
    while (fread(&c, 1, 1, fp) == 1)
        freq[c]++;
}

// 构建 Huffman 树
HuffNode* buildTree(int freq[]) {//选出频率最小的两个节点，合并成父节点，循环到只剩一个根节点 
    HuffNode *nodes[256];
    int cnt = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0)
            nodes[cnt++] = createNode((unsigned char)i, freq[i]);
    }
    if (cnt == 0) return NULL;

    while (cnt > 1) {
        int min1 = 0, min2 = 1;
        if (nodes[min1]->freq > nodes[min2]->freq) {
            int t = min1; min1 = min2; min2 = t;
        }
        for (int i = 2; i < cnt; i++) {
            if (nodes[i]->freq < nodes[min1]->freq) {
                min2 = min1;
                min1 = i;
            } else if (nodes[i]->freq < nodes[min2]->freq) {
                min2 = i;
            }
        }
        HuffNode *parent = createNode(0, nodes[min1]->freq + nodes[min2]->freq);
        parent->left = nodes[min1];
        parent->right = nodes[min2];
        nodes[min1] = parent;
        nodes[min2] = nodes[--cnt];
    }
    return nodes[0];
}

// 生成 Huffman 编码表
void generateCode(HuffNode *root, Code table[], char buf[], int depth) {
    if (!root) return;
    if (!root->left && !root->right) {
        table[root->ch].len = depth;
        buf[depth] = '\0';
        strcpy(table[root->ch].code, buf);
        return;
    }
    buf[depth] = '0';
    generateCode(root->left, table, buf, depth + 1);
    buf[depth] = '1';
    generateCode(root->right, table, buf, depth + 1);
}

// 释放 Huffman 树
void freeTree(HuffNode *root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
}

// ==================== 递归创建目录 ====================
void ensureDirectoryExists(const char *path) {
    char tmp[MAX_PATH];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '\\')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = 0;
            _mkdir(tmp);
            *p = '\\';
        }
    }
    _mkdir(tmp);
}

// ==================== 单文件压缩 ====================
void compress(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    if (!in) {
        printf("Error: cannot open input file %s\n", src);
        return;
    }
    if (!out) {
        printf("Error: cannot create output file %s\n", dst);
        fclose(in);
        return;
    }

    fseek(in, 0, SEEK_END);
    long originalSize = ftell(in);
    fseek(in, 0, SEEK_SET);

    int freq[256] = {0};
    countFreq(in, freq);
    fseek(in, 0, SEEK_SET);

    fwrite(freq, sizeof(int), 256, out);
    fwrite(&originalSize, sizeof(long), 1, out);

    if (originalSize == 0) {
        fclose(in);
        fclose(out);
        return;
    }

    HuffNode *root = buildTree(freq);
    if (!root) {
        fclose(in);
        fclose(out);
        return;
    }

    Code table[256] = {0};
    char buf[256];
    generateCode(root, table, buf, 0);

    unsigned char bitBuf = 0;
    int bitCnt = 0;
    unsigned char c;
    while (fread(&c, 1, 1, in) == 1) {
        for (int i = 0; i < table[c].len; i++) {
            bitBuf <<= 1;
            if (table[c].code[i] == '1')
                bitBuf |= 1;
            bitCnt++;
            if (bitCnt == 8) {
                fwrite(&bitBuf, 1, 1, out);
                bitBuf = 0;
                bitCnt = 0;
            }
        }
    }
    if (bitCnt > 0) {
        bitBuf <<= (8 - bitCnt);
        fwrite(&bitBuf, 1, 1, out);
    }

    fclose(in);
    fclose(out);
    freeTree(root);
}

// ==================== 单文件解压 ====================
void decompress(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    if (!in) {
        printf("Error: cannot open input file %s\n", src);
        return;
    }
    if (!out) {
        printf("Error: cannot create output file %s\n", dst);
        fclose(in);
        return;
    }

    int freq[256] = {0};
    fread(freq, sizeof(int), 256, in);

    long originalSize;
    fread(&originalSize, sizeof(long), 1, in);

    if (originalSize == 0) {
        fclose(in);
        fclose(out);
        return;
    }

    HuffNode *root = buildTree(freq);
    if (!root) {
        printf("Error: failed to build Huffman tree (corrupted file)\n");
        fclose(in);
        fclose(out);
        return;
    }

    HuffNode *p = root;
    unsigned char c;
    long decodedBytes = 0;
    int bitPos = 0;
    unsigned char currentByte;

    while (decodedBytes < originalSize && fread(&currentByte, 1, 1, in) == 1) {
        for (bitPos = 7; bitPos >= 0 && decodedBytes < originalSize; bitPos--) {
            int bit = (currentByte >> bitPos) & 1;
            p = bit ? p->right : p->left;
            if (!p->left && !p->right) {
                fwrite(&p->ch, 1, 1, out);
                decodedBytes++;
                p = root;
            }
        }
    }

    if (decodedBytes != originalSize) {
        printf("Warning: decoded %ld bytes, expected %ld (file may be truncated)\n",
               decodedBytes, originalSize);
    }

    fclose(in);
    fclose(out);
    freeTree(root);
}

// ==================== 多文件打包压缩（核心修正版）====================
void zipFiles(int fileCount, char *files[], const char *dest) {
    FILE *out = fopen(dest, "wb");
    if (!out) {
        printf("Error: cannot create archive %s\n", dest);
        return;
    }

    // 先统计有效文件数量
    int validCount = 0;
    for (int i = 0; i < fileCount; i++) {
        FILE *test = fopen(files[i], "rb");
        if (test) {
            validCount++;
            fclose(test);
        } else {
            printf("Warning: cannot open file %s, skipping\n", files[i]);
        }
    }

    fwrite(&validCount, sizeof(int), 1, out);

    for (int i = 0; i < fileCount; i++) {
        FILE *in = fopen(files[i], "rb");
        if (!in) continue;   // 已跳过

        fseek(in, 0, SEEK_END);
        long originalSize = ftell(in);
        fseek(in, 0, SEEK_SET);

        int freq[256] = {0};
        countFreq(in, freq);
        fseek(in, 0, SEEK_SET);

        int nameLen = strlen(files[i]) + 1;
        fwrite(&nameLen, sizeof(int), 1, out);
        fwrite(files[i], 1, nameLen, out);

        fwrite(&originalSize, sizeof(long), 1, out);
        fwrite(freq, sizeof(int), 256, out);

        if (originalSize == 0) {
            fclose(in);
            continue;
        }

        HuffNode *root = buildTree(freq);
        if (!root) {
            fclose(in);
            continue;
        }

        Code table[256] = {0};
        char buf[256];
        generateCode(root, table, buf, 0);

        unsigned char bitBuf = 0;
        int bitCnt = 0;
        unsigned char c;
        while (fread(&c, 1, 1, in) == 1) {
            for (int j = 0; j < table[c].len; j++) {
                bitBuf <<= 1;
                if (table[c].code[j] == '1')
                    bitBuf |= 1;
                bitCnt++;
                if (bitCnt == 8) {
                    fwrite(&bitBuf, 1, 1, out);
                    bitBuf = 0;
                    bitCnt = 0;
                }
            }
        }
        if (bitCnt > 0) {
            bitBuf <<= (8 - bitCnt);
            fwrite(&bitBuf, 1, 1, out);
        }

        freeTree(root);
        fclose(in);
        printf("Compressed: %s\n", files[i]);
    }
    fclose(out);
}

// ==================== 多文件解包解压（修正版） ====================
void unzipFiles(const char *src) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        printf("Error: cannot open archive %s\n", src);
        return;
    }

    int fileCount;
    if (fread(&fileCount, sizeof(int), 1, in) != 1) {
        printf("Error: invalid archive format\n");
        fclose(in);
        return;
    }

    for (int i = 0; i < fileCount; i++) {
        int nameLen;
        if (fread(&nameLen, sizeof(int), 1, in) != 1) {
            printf("Error: unexpected end of archive (file %d)\n", i + 1);
            break;
        }
        char *filename = (char*)malloc(nameLen);
        if (fread(filename, 1, nameLen, in) != (size_t)nameLen) {
            free(filename);
            printf("Error: cannot read filename for file %d\n", i + 1);
            break;
        }

        long originalSize;
        if (fread(&originalSize, sizeof(long), 1, in) != 1) {
            free(filename);
            printf("Error: cannot read size for %s\n", filename);
            break;
        }

        int freq[256];
        if (fread(freq, sizeof(int), 256, in) != 256) {
            free(filename);
            printf("Error: cannot read frequency table for %s\n", filename);
            break;
        }

        // 确保文件所在目录存在
        char *lastSlash = strrchr(filename, '\\');
        if (lastSlash) {
            *lastSlash = '\0';
            ensureDirectoryExists(filename);
            *lastSlash = '\\';
        }

        FILE *out = fopen(filename, "wb");
        if (!out) {
            printf("Error: cannot create output file %s\n", filename);
            free(filename);
            // 无法跳过该文件的数据流，因为不知道压缩数据长度，只能整体失败
            fclose(in);
            return;
        }

        if (originalSize == 0) {
            fclose(out);
            free(filename);
            continue;
        }

        HuffNode *root = buildTree(freq);
        if (!root) {
            printf("Error: failed to build tree for %s\n", filename);
            fclose(out);
            free(filename);
            fclose(in);
            return;
        }

        HuffNode *p = root;
        long decoded = 0;
        unsigned char byte;
        int bitPos;
        while (decoded < originalSize && fread(&byte, 1, 1, in) == 1) {
            for (bitPos = 7; bitPos >= 0 && decoded < originalSize; bitPos--) {
                int bit = (byte >> bitPos) & 1;
                p = bit ? p->right : p->left;
                if (!p->left && !p->right) {
                    fwrite(&p->ch, 1, 1, out);
                    decoded++;
                    p = root;
                }
            }
        }

        if (decoded != originalSize) {
            printf("Warning: %s decoded %ld / %ld bytes\n", filename, decoded, originalSize);
        }

        freeTree(root);
        fclose(out);
        printf("Restored: %s\n", filename);
        free(filename);
    }
    fclose(in);
}

// ==================== 通配符支持函数（修正版）====================
int scanWildFiles(const char *pattern, char ***fileArr) {
    struct _finddata_t fd;
    intptr_t handle;
    int count = 0;
    char **list = NULL;

    char fullPattern[MAX_PATH];
    strcpy(fullPattern, pattern);

    // 统一分隔符为反斜杠
    for (char *p = fullPattern; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    char dir[MAX_PATH];
    char wild[MAX_PATH];
    char *lastSep = strrchr(fullPattern, '\\');

    if (lastSep) {
        *lastSep = '\0';
        strcpy(dir, fullPattern);
        strcpy(wild, lastSep + 1);
        *lastSep = '\\';   // 恢复，保持原始字符串完整性（不影响后续 _findfirst 参数）
    } else {
        dir[0] = '.';
        dir[1] = '\0';
        strcpy(wild, fullPattern);
    }

    // 确保目录以反斜杠结尾
    size_t dlen = strlen(dir);
    if (dlen > 0 && dir[dlen - 1] != '\\') {
        strcat(dir, "\\");
    }

    // 使用统一后的路径进行搜索
    handle = _findfirst(fullPattern, &fd);
    if (handle == -1) return 0;

    do {
        if (!(fd.attrib & _A_SUBDIR)) {
            char fullPath[MAX_PATH];
            snprintf(fullPath, sizeof(fullPath), "%s%s", dir, fd.name);

            char **newList = (char**)realloc(list, (count + 1) * sizeof(char*));
            if (!newList) {
                // 内存失败：释放已收集的资源
                for (int i = 0; i < count; i++) free(list[i]);
                free(list);
                _findclose(handle);
                return 0;
            }
            list = newList;
            list[count] = _strdup(fullPath);
            count++;
        }
    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);
    *fileArr = list;
    return count;
}

// ==================== 主程序 ====================
int main(int argc, char *argv[]) {
    clock_t start = clock();
    char **autoFiles = NULL;
    int autoCnt = 0;

    if (argc == 4 && strcmp(argv[1], "zip") == 0) {
        // 检查是否是通配符模式（包含 * 或 ?）
        if (strchr(argv[2], '*') != NULL || strchr(argv[2], '/') != NULL) {
            autoCnt = scanWildFiles(argv[2], &autoFiles);
            if (autoCnt <= 0) {
                printf("Warning: no files match pattern %s\n", argv[2]);
                return 1;
            }
            zipFiles(autoCnt, autoFiles, argv[3]);
            for (int i = 0; i < autoCnt; i++) free(autoFiles[i]);
            free(autoFiles);
        } else {
            // 单个文件压缩（兼容旧用法：zip src dst）
            compress(argv[2], argv[3]);
        }
    }
    else if (argc == 4 && strcmp(argv[1], "compress") == 0) {
        compress(argv[2], argv[3]);
    }
    else if (argc == 4 && strcmp(argv[1], "decompress") == 0) {
        decompress(argv[2], argv[3]);
    }
    else if (argc >= 4 && strcmp(argv[1], "zip") == 0) {
        // 手动指定多个文件：zip f1 f2 f3 out.huff
        zipFiles(argc - 3, &argv[2], argv[argc-1]);
    }
    else if (argc == 3 && strcmp(argv[1], "unzip") == 0) {
        unzipFiles(argv[2]);
    }
    else {
        printf("Usage:\n");
        printf("  Single compress: %s compress <src> <dst>\n", argv[0]);
        printf("  Single decompress: %s decompress <src> <dst>\n", argv[0]);
        printf("  Single compress (zip): %s zip <src> <dst>\n", argv[0]);
        printf("  Wildcard compress: %s zip \"folder\\*.txt\" out.huff\n", argv[0]);
        printf("  Multi files: %s zip f1 f2 f3 ... out.huff\n", argv[0]);
        printf("  Unpack: %s unzip <archive>\n", argv[0]);
        return 0;
    }

    clock_t end = clock();
    printf("Time: %.4f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    printf("Done.\n");
    return 0;
}
