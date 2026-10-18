const uint CHECKERBOARD_FINE_SLOTS   = 4u;
const uint CHECKERBOARD_COARSE_SLOTS = 16u;

const ivec2 CHECKERBOARD_FINE_OFFSETS[CHECKERBOARD_FINE_SLOTS] = ivec2[](
    ivec2(0, 0), ivec2(1, 1), ivec2(1, 0), ivec2(0, 1)
);

const ivec2 CHECKERBOARD_COARSE_OFFSETS[CHECKERBOARD_COARSE_SLOTS] = ivec2[](
    ivec2(0, 0), ivec2(2, 2), ivec2(2, 0), ivec2(0, 2),
    ivec2(1, 1), ivec2(3, 3), ivec2(3, 1), ivec2(1, 3),
    ivec2(1, 0), ivec2(3, 2), ivec2(3, 0), ivec2(1, 2),
    ivec2(0, 1), ivec2(2, 3), ivec2(2, 1), ivec2(0, 3)
);

ivec2 checkerboard_offset(uint cell, uint frame_index)
{
    if (cell <= 1u) {
        return ivec2(0);
    }

    if (cell == 2u) {
        return CHECKERBOARD_FINE_OFFSETS[frame_index % CHECKERBOARD_FINE_SLOTS];
    }

    return CHECKERBOARD_COARSE_OFFSETS[frame_index % CHECKERBOARD_COARSE_SLOTS];
}
