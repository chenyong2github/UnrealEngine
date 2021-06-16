
struct BCFHEADER
{
    U32 tag; // BCF1
    U8 version;
    U8 channels;
    U16 rate;
    U32 sample_count;
    U16 max_comp_space_needed;
    U16 flags;
    U32 output_file_size;
    U16 blocks_per_seek_table_entry;
    U16 seek_table_entry_count;
};
