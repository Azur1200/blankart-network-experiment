const std = @import("std");

pub fn build(b: *std.Build) void {
	const target = b.standardTargetOptions(.{});
	const optimize = b.standardOptimizeOption(.{});

	// this is just unit tests for now, so the root source file is the tests file
	const blankart = b.addModule("blankart", .{
		.root_source_file = b.path("src/tests.zig"),
		.target = target,
		.optimize = optimize,
	});

	blankart.addIncludePath(b.path("src")); // -Isrc
	blankart.linkSystemLibrary("c", .{}); // -lc
	blankart.addCSourceFiles(.{
		.root = b.path("src"),
		// anything you need to test goes here
		.files = &.{ "m_fixed.c", "tables.c" },
	});

	const blantests = b.addTest(.{
		.root_module = blankart,
	});
	const test_step = b.step("test", "Run tests");
	test_step.dependOn(&b.addRunArtifact(blantests).step);
}
