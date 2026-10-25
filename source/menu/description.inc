// Notes stay on SD. Only one wrapped page is kept in RAM, regardless of note size.
// Every read is bounded by the current record and takes place before drawing.
static bool descriptionByte(u32 offset, u8* value)
{
    if (!readByte(offset, value))
    {
        sDescriptionError = true;
        return false;
    }
    return true;
}

static bool descriptionStringEnd(u32* offset, u32 end)
{
    u8 c;
    while (*offset < end)
    {
        if (!descriptionByte((*offset)++, &c)) return false;
        if (!c) return true;
    }
    sDescriptionError = true;
    return false;
}

// Decode a character without swallowing a terminator or a non-continuation byte.
static char descriptionChar(u32* offset)
{
    u8 c;
    if (*offset >= sDescriptionEnd || !descriptionByte((*offset)++, &c))
        return 0;
    if (c == '\r')
    {
        u8 next;
        if (*offset < sDescriptionEnd && descriptionByte(*offset, &next) && next == '\n')
            (*offset)++;
        return '\n';
    }
    if (c == '\n') return '\n';
    if (c < 0x20 || c == 0x7F) return ' ';
    if (c < 0x80) return c;
    u32 count = c >= 0xC2 && c <= 0xDF ? 1 : (c >= 0xE0 && c <= 0xEF ? 2 : (c >= 0xF0 && c <= 0xF4 ? 3 : 0));
    if (!count) return '?';
    u32 codePoint = c & (0x3F >> count);
    for (u32 i = 0; i < count; i++)
    {
        if (*offset >= sDescriptionEnd || !descriptionByte(*offset, &c) || (c & 0xC0) != 0x80)
            return '?';
        (*offset)++;
        codePoint = (codePoint << 6) | (c & 0x3F);
    }
    if (codePoint < (count == 1 ? 0x80u : (count == 2 ? 0x800u : 0x10000u)) ||
        codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        return '?';
    return codePoint >= 0xC0 && codePoint <= 0xFF ? sLatin1ToAscii[codePoint - 0xC0] : '?';
}

// Returns the next line's byte offset. Long words are split, other words wrap.
static u32 descriptionLine(u32 offset, char* line)
{
    u32 length = 0, spaceLength = 0, afterSpace = 0;
    bool newline = false;
    while (offset < sDescriptionEnd && length < TEXT_COLUMNS)
    {
        char c = descriptionChar(&offset);
        if (!c) break;
        if (c == '\n') { newline = true; break; }
        if (c == ' ') { spaceLength = length; afterSpace = offset; }
        line[length++] = c;
    }
    if (!newline && length == TEXT_COLUMNS && offset < sDescriptionEnd)
    {
        u32 next = offset;
        char c = descriptionChar(&next);
        if (c == '\n') offset = next;
        else if (c == ' ')
        {
            // Whitespace at a soft wrap is not the start of a new blank line.
            do
            {
                offset = next;
                c = descriptionChar(&next);
            } while (c == ' ');
            if (c == '\n') offset = next;
        }
        else if (afterSpace && spaceLength)
        {
            length = spaceLength;
            offset = afterSpace;
        }
    }
    while (length && line[length - 1] == ' ') length--;
    line[length] = 0;
    return offset;
}

static void loadDescriptionPage(void)
{
    memset(sDescriptionRows, 0, sizeof(sDescriptionRows));
    u32 offset = sDescriptionPageOffset;
    for (u32 row = 0; row < DESCRIPTION_ROWS && offset < sDescriptionEnd && !sDescriptionError; row++)
        offset = descriptionLine(offset, sDescriptionRows[row]);
    sDescriptionNextPage = offset < sDescriptionEnd && !sDescriptionError ? offset : 0;
    if (sDescriptionError || sDescriptionStart == sDescriptionEnd)
    {
        memset(sDescriptionRows, 0, sizeof(sDescriptionRows));
        const char* message = sDescriptionError ? "Error reading description." : "No description available.";
        for (u32 i = 0; message[i]; i++) sDescriptionRows[0][i] = message[i];
    }
}

static void openDescription(void)
{
    if (!getEntryCount()) return;
    sDescriptionOpen = true;
    sDescriptionError = false;
    sDescriptionStart = sDescriptionEnd = 0;
    sDescriptionPage = 1;
    u32 item = getEntryItem(sCursor);
    const cheat_menu_data_t* data = getData();
    if (item < data->itemCount)
    {
        u32 start = getItemOffset(item);
        u32 end = item + 1 < data->itemCount ? getItemOffset(item + 1) : data->blockSize;
        if (start >= end || end > data->blockSize || end - start <= 4)
            sDescriptionError = true;
        else
        {
            u32 offset = start + 4;
            if (descriptionStringEnd(&offset, end))
            {
                sDescriptionStart = offset;
                if (descriptionStringEnd(&offset, end)) sDescriptionEnd = offset - 1;
            }
        }
    }
    sDescriptionPageOffset = sDescriptionStart;
    loadDescriptionPage();
}

static void moveDescriptionPage(bool forward)
{
    if (sDescriptionError) return;
    if (forward)
    {
        if (!sDescriptionNextPage) return;
        sDescriptionPageOffset = sDescriptionNextPage;
        sDescriptionPage++;
    }
    else
    {
        if (sDescriptionPage <= 1) return;
        // Re-scan on backwards navigation rather than storing an unbounded index.
        u32 offset = sDescriptionStart;
        char line[TEXT_COLUMNS + 1];
        for (u32 page = 1; page < sDescriptionPage - 1 && !sDescriptionError; page++)
            for (u32 row = 0; row < DESCRIPTION_ROWS && offset < sDescriptionEnd; row++)
                offset = descriptionLine(offset, line);
        sDescriptionPageOffset = offset;
        sDescriptionPage--;
    }
    loadDescriptionPage();
}
