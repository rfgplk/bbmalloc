SECTIONS
{
  .heap (NOLOAD) : ALIGN(4096)
  {
    __heap_start = .;
    . += 0x40000;
    __heap_end = .;
  } > RAM
}
