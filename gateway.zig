// gateway.zig - EdgeTG config author in Zig
// Compile: zig build-exe gateway.zig -O ReleaseFast
// Run: ./gateway

const std = @import("std");

// The human-readable config as a struct
const Config = struct {
    topology: []const u8 = "_/_\\",
    sensor_id: []const u8 = "sensor_01",
    temp: f32 = 25.3,
};

pub fn main() !void {
    const config = Config{};
    
    // Open wire payload file
    var file = try std.fs.cwd().createFile("wire_payload.bin", .{});
    defer file.close();
    var writer = file.writer();
    
    // 1. Write manifest version (1 byte)
    try writer.writeInt(u8, 2);
    
    // 2. Write topology length (1 byte) + topology string
    try writer.writeInt(u8, @intCast(config.topology.len));
    try writer.writeAll(config.topology);
    
    // 3. Write values blob (4-byte string len + string + 4-byte float)
    try writer.writeIntLittle(u32, @intCast(config.sensor_id.len));
    try writer.writeAll(config.sensor_id);
    try writer.writeIntLittle(f32, config.temp);
    
    std.debug.print("Gateway (Zig): Sent config to MCU.\n", .{});
}
