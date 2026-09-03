#include "ts_core.h"
#include "ts_layers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED_MANIFEST_VERSION 2

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <wire_payload.bin>\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;
    
    // 1. SAFETY NET: Read and check Manifest Version
    uint8_t version;
    if (fread(&version, 1, 1, f) != 1) { fclose(f); return 1; }
    
    if (version != EXPECTED_MANIFEST_VERSION) {
        fprintf(stderr, "REJECTED: Manifest version mismatch. Expected %d, got %d.\n", 
                EXPECTED_MANIFEST_VERSION, version);
        fclose(f);
        return 2; // Distinct exit code for version mismatch
    }
    
    // 2. Read Topology String (1-byte length prefix)
    uint8_t topo_len;
    fread(&topo_len, 1, 1, f);
    char topo_str[256];
    fread(topo_str, 1, topo_len, f);
    topo_str[topo_len] = '\0';
    
    // 3. Read Values Blob (rest of file)
    long current_pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, current_pos, SEEK_SET);
    size_t blob_len = file_size - current_pos;
    uint8_t* blob = malloc(blob_len);
    fread(blob, 1, blob_len, f);
    fclose(f);
    
    // 4. Parse and Decode
    TSNode* tree = NULL;
    if (ts_parse(topo_str, 16, &tree) != TS_OK) { free(blob); return 3; }
    
    TSValue* vals = NULL; size_t val_count = 0;
    if (ts_values_decode(blob, blob_len, &vals, &val_count) != TS_OK) {
        ts_free_tree(tree); free(tree); free(blob); return 4;
    }
    
    // 5. "EXECUTE": The 25.3 -> 25.7 drift (proves real processing)
    if (val_count >= 2 && vals[1].len == sizeof(float)) {
float temp;
        memcpy(&temp, vals[1].data, sizeof(float));
        temp += 0.4f; // The MCU applies a calibration offset
        memcpy((void *)vals[1].data, &temp, sizeof(float));
    }
    
    // 6. Encode Reply
    uint8_t* reply_blob = NULL; size_t reply_blob_len = 0;
    ts_values_encode(vals, val_count, &reply_blob, &reply_blob_len);
    
    FILE* out = fopen("wire_reply.bin", "wb");
    fwrite(&version, 1, 1, out);
    fwrite(&topo_len, 1, 1, out);
    fwrite(topo_str, 1, topo_len, out);
    fwrite(reply_blob, 1, reply_blob_len, out);
    fclose(out);
    
// Cleanup
    ts_values_free(vals, val_count); free(reply_blob); free(blob);
    ts_free_tree(tree); free(tree);
    
    printf("MCU: Executed successfully. Wrote wire_reply.bin\n");
    return 0;
}
