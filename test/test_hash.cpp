#include "picosha2.h"
#include "md5.h"
#include <fstream>
#include <vector>
#include <cstdio>

int main() {
    std::ifstream f("testfiles/sp/anata_g24.bme", std::ios::binary | std::ios::ate);
    size_t sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    
    printf("File size: %zu\n", sz);
    printf("SHA256: %s\n", picosha2::hash256_hex_string(buf).c_str());
    printf("MD5: %s\n", md5::hash_hex_string(buf).c_str());
    return 0;
}
