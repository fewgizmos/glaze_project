const int  CHECKERBOARD_SIZE  = 4;
const uint CHECKERBOARD_SLOTS = 16u;

const ivec2 CHECKERBOARD_OFFSETS[CHECKERBOARD_SLOTS] = ivec2[](
    ivec2(0, 0), ivec2(2, 2), ivec2(2, 0), ivec2(0, 2),
    ivec2(1, 1), ivec2(3, 3), ivec2(3, 1), ivec2(1, 3),
    ivec2(1, 0), ivec2(3, 2), ivec2(3, 0), ivec2(1, 2),
    ivec2(0, 1), ivec2(2, 3), ivec2(2, 1), ivec2(0, 3)
);

ivec2 checkerboard_offset(uint frame_index)
{
    return CHECKERBOARD_OFFSETS[frame_index % CHECKERBOARD_SLOTS];
}
