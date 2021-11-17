
/*

Oodle example :

create "oodle_example_input_file" if not found

*/

static void make_example_input(const char * in_name)
{
    OO_SINTa size = 3*1024*1024;
    
    OodleXFileInfo info;
    if ( OodleXIOQ_GetInfoByName_AsyncAndWait(in_name,&info,OodleFileNotFoundIsAnError_No) )
    {
        if ( info.size != size )
        {
            fprintf(stderr,"make_example_input (%s) : file exists but not expected size!?\n",in_name);          
        }
        return;
    }
    
    OO_U8 * buffer = (OO_U8 *) OodleXMalloc_IOAligned(size);

    OO_U64 state = 0x0102030405060708ULL;

    OO_SINTa count = size/8;
    OO_U64 * buf64 = (OO_U64 *)buffer;

    for(OO_SINTa i=0;i<count;i++)
    {
        OO_SINTa rand = i * 2147001325 + 715136305;
        rand = 0x31415926 ^ ((rand >> 16) + (rand << 16));

        OO_S32 r = (((OO_S32)rand) % 257); // LZ compress 3145728 -> 1539959
        //r = (r*r)/4;
        
        state += r;

        if ( (i&31) == 0 ) state = 0x0102030405060708ULL * (i>>5);

        state ^= ((state >> (rand & 31)) & 0x0F0F0F);

        if ( (i&3) == 0 ) state &= 0x003FFFFFFFFFFFFFULL;
        //if ( (i&3) == 0 ) state &= 0xFFFFFFFFFFFF0000ULL;

        buf64[i] = state;
    }
        
    OodleXHandle h = OodleXIOQ_OpenWriteWholeFileClose_Async(in_name,buffer,size);
    // can't do anything funny here with leaving the write pending
    // because some of the examples read using stdio , so we must flush
    OodleX_Wait(h,OodleXHandleDeleteIfDone_Yes);
    
    OodleXFree_IOAligned(buffer);
}
