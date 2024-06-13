#include <time.h>
#include "common.c"

int random_int(int min, int max) {
    return rand() % (max-min + 1) + min;
}


void convert_key_hex(char* src, char *target){
    char *hex = "0123456789abcdef";
    unsigned char k;
    for (int i = 0, j = 0; i < 256; i++) {
        k = (unsigned char) src[i];
        target[j++] = hex[k/16];
        target[j++] = hex[k%16];
        // printf("%d %d / ",  k/16, k%16);
    }
}

void print_key(char* key){
        printf("   ");
        for (int i = 1; i < 17; i++) {
            if (i < 10) {
                printf(" %d   ", i);
            } else {
                printf("%d   ", i);
            }
        }
    int line = 1;
    for (int i = 0; i < 256; i++) {
        if (i % 16 == 0) {
            printf("\n");
                printf(line < 10 ? " %d " : "%d ", line);
            line++;   
        }
        printf("%d, ", (unsigned char) key[i]);
        if (key[i] < 0) {
        } else if (key[i] < 10) {
            printf("  ");
        } else if (key[i] < 100) {
            printf(" ");
        }
    }
    printf("\n");
}


int main(int argc, char const *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <0:key/1:key_table> (<check:1~255>)\n", argv[0]);
        return 0;
    }
    
    srand( (unsigned) time(NULL));

    int i, idx;
    char key_hex[513];
    char key_en_t[256];
    for (i = 0; i < 256; i++) key_en_t[i] = i;
    for (i = 1; i < 256; i++) {
        idx = random_int(1, 255);
        if (idx == i) {
            i--;
            continue;
        }
        // exchange
        key_en_t[i] ^= key_en_t[idx];
        key_en_t[idx] ^= key_en_t[i];
        key_en_t[i] ^= key_en_t[idx];
    }
    key_hex[512] = '\0';
    convert_key_hex(key_en_t, key_hex);
    
    key_init(key_hex);

    int type = atoi(argv[1]);
    switch (type) {
        case 0:
            printf("--- key :---\n%s\n", key_hex);
            break;
        case 1:
            print_key(key_en);
            convert_key_hex(key_en, key_hex);
            printf("---key en hex---\n%s\n", key_hex);
            printf("---------------------------------------------------------------------\n");
            print_key(key_de);
            convert_key_hex(key_de, key_hex);
            printf("---key de hex---\n%s\n", key_hex);
            break;
        default:
            break;
    }
    if (argc > 2) {
        idx = atoi(argv[2]);
        unsigned char en_idx = (unsigned char)key_en[idx];
        printf("en[%d]=%d de[%d]=%d\n", idx, en_idx, en_idx, (unsigned char)key_de[en_idx]); 
    }
    return 0;
}
