const std = @import("std");

const C = @cImport({
	// include the headers you need for testing here
	// also make sure to add the corresponding source files in build.zig until
	// the symbol errors go away :face_holding_back_tears:
	@cInclude("m_fixed.h");
});

const fixed_t = i32;
const FRACUNIT: fixed_t = 65536;
const FRACBITS = 16;

test "FixedSqrt d_main.cpp"
{
	try std.testing.expectEqual(C.FixedSqrt(0x7FFF0000), C.FixedSqrt64(0x7FFF0000));
	try std.testing.expectEqual(C.FixedSqrt(0x40000), C.FixedSqrt64(0x40000));

	// You should probably generate the weird number with RANDOM.org
	const WEIRDNUMBER = 3886284; // 59.3 fracunits; this should approximate to around 7
	try std.testing.expectEqual(C.FixedSqrt(WEIRDNUMBER), C.FixedSqrt64(WEIRDNUMBER));
}


test "FixedSqrt m_fixed.c"
{
	var a: i64 = 0;
	// not enough time in the world for all 2 billion values...
	// prime numbers to the rescue
	while (a <= std.math.maxInt(i32)) : (a += 97) {
		const c: fixed_t = C.FixedSqrt(@intCast(a));
		const d: fixed_t = @intFromFloat(@sqrt(@as(f64, @floatFromInt(a)) / FRACUNIT) * FRACUNIT);
		try std.testing.expect(@abs(c - d) <= 1);
	}
}
