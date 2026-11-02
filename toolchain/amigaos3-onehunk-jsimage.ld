/* GFOpenBrowser (DalsinAI/gfopenbrowser)
 * Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
 *
 * One-hunk link of a jsimage client (scripts/jsimage.py, src/jslib/
 * jsimage.h): amigaos3-onehunk.ld with two additions.
 *
 * - The program starts at jsimage_entry (jsimage_client.c), which loads
 *   JavaScriptEngine and fixes the program up before the C library's own
 *   start-up (jsimage_cstart, then ncrt0) runs.
 * - At the end of the hunk, jsimage-imports.ld (found through -L; written
 *   by "jsimage.py script") gives each name the program takes from
 *   JavaScriptEngine as __jsimage_base plus its offset in that program.
 *   The names are inside the output section, so they are relative to the
 *   hunk: ld resolves pc-relative references to them, and gives absolute
 *   ones a HUNK_RELOC32, as it does for the hunk's own names. They point
 *   past the end of the hunk until the program is fixed up.
 */
OUTPUT_FORMAT("amiga")
OUTPUT_ARCH(m68k)
SECTIONS
{
  PROVIDE(___machtype = 0x0);
  . = 0x0;
  .text :
  {
    KEEP(*(.text.jsimage_entry))
    KEEP(*(.rodata.jsimage_entry))
    _jsimage_cstart = .;
    __stext = .;
    *(.text)
    *(.text.main)
    *(.text*)
    *(_*)
    *(.rodata*)
    *(.data.rel.ro*)
    *(.gnu.linkonce.t.*)
    *(.gnu.linkonce.r.*)
    *(.gcc_except_table*)
    *(SORT_BY_NAME(.list___EH_FRAME*))
    *(SORT_BY_NAME(.list_*))
    *(.end_of_lists)
    ___datadata_relocs = .;
    __etext = .;
    . = ALIGN(4);
    __sdata = .;
    *(.data)
    *(SORT_BY_NAME(.data.*))
    CONSTRUCTORS
    *(SORT_BY_NAME(.dlist___EH_FRAME_OBJECT*))
    *(SORT_BY_NAME(.dlist_*))
    *(.end_of_dlists)
    *(.data.__EH_FRAME_OBJECT__*)
    *(.gnu.linkonce.d.*)
    __edata = .;
    . = ALIGN(4);
    __bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    __end = .;
    __jsimage_base = .;
    INCLUDE jsimage-imports.ld
  }
  ___text_size = SIZEOF(.text);
  ___data_size = __edata - __sdata;
  ___bss_size = __end - __bss_start;
  .datachip :
  {
    *(.datachip)
  }
  .bsschip :
  {
    *(.bsschip)
  }
  .debug_frame 0 : { __debug_frame_start = .; *(.debug_frame) __debug_frame_end = .; }
  .debug_info 0 : { __debug_info_start = .; *(.debug_info) *(.gnu.linkonce.wi.*) __debug_info_end = .; }
  .debug_abbrev 0 : { __debug_abbrev_start = .; *(.debug_abbrev) __debug_abbrev_end = .; }
  .debug_loclists 0 : { __debug_loclists_start = .; *(.debug_loclists) __debug_loclists_end = .; }
  .debug_aranges 0 : { __debug_aranges_start = .; *(.debug_aranges) __debug_aranges_end = .; }
  .debug_rnglists 0 : { __debug_rnglists_start = .; *(.debug_rnglists) __debug_rnglists_end = .; }
  .debug_line 0 : { __debug_line_start = .; *(.debug_line) *(.debug_line.*) __debug_line_end = .; }
  .debug_str 0 : { __debug_str_start = .; *(.debug_str) __debug_str_end = .; }
  .debug_line_str 0 : { __debug_line_str_start = .; *(.debug_line_str) __debug_line_str_end = .; }
  .debug_types 0 : { __debug_types_start = .; *(.debug_types) __debug_types_end = .; }
  .debug_macro 0 : { __debug_macro_start = .; *(.debug_macro) __debug_macro_end = .; }
  .debug_ranges 0 : { __debug_ranges_start = .; *(.debug_ranges) __debug_ranges_end = .; }
  .debug_addr 0 : { __debug_addr_start = .; *(.debug_addr) __debug_addr_end = .; }
  .debug_str_offsets 0 : { __debug_str_offsets_start = .; *(.debug_str_offsets) __debug_str_offsets_end = .; }
}
