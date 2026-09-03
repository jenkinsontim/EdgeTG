// gateway.c3 - EdgeTG config author in C3
// Compile: c3c compile gateway.c3 -o gateway_c3
// Run: ./gateway_c3

import std::io;

fn void main()
{
    // The human-readable config
    char* topology = "_/_\\";
    char* sensor_id = "sensor_01";
    float temp = 25.3;
    
    // Open wire payload file
    File f = File::open_write("wire_payload.bin");
    
    // 1. Write manifest version (1 byte)
    f.write(2);
    
    // 2. Write topology length (1 byte) + topology string
    f.write(topology.len);
    f.write(topology);
    
    // 3. Write values blob (4-byte string len + string + 4-byte float)
    uint32_t str_len = sensor_id.len;
    f.write(&str_len, 4);
    f.write(sensor_id);
    f.write(&temp, 4);
    
    f.close();
    io::printn("Gateway (C3): Sent config to MCU.");
}
