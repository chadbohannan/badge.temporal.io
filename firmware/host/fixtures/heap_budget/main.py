# Test fixture for the host harness: fills the Python heap with 100 KB blocks until
# MemoryError and shows how many fit. With REPLAY_MP_HEAP_BUDGET at 4 MB the answer
# is 40, on the badge and on the host alike.
__title__ = "Heap budget"
__order__ = -5

import gc
import time

blocks = []
try:
    while True:
        blocks.append(bytearray(100000))
except MemoryError:
    count = len(blocks)
del blocks
gc.collect()

oled_clear()
oled_set_cursor(4, 20)
oled_print("BLOCKS %d" % count)
oled_set_cursor(4, 36)
oled_print("FREE %d" % gc.mem_free())
oled_show()

while not button_pressed(BTN_BACK):
    time.sleep_ms(20)
