
# Execute mass erase every time `program_via_algorithm` is called
MEMPROG_OOCD_FLAG_ERASE             = 0x00000001
# Execute default verification algorithm at the end of `program_via_algorithm` (read back memory over SWD)
#  This obviously won't work for memory that is not directly accessible over SWD
MEMPROG_OOCD_FLAG_VERIFY            = 0x00000002
