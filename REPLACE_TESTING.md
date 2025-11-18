# REPLACE Command Testing Guide

The REPLACE command allows word-level editing within a sentence through an interactive loop.

## Usage
```
REPLACE <filename> <sentence_id>
```

Then enter interactive mode:
- `<word_index> <new_content>` - Replace word at index (1-indexed)
- `<word_index> ""` - Delete word at index
- `<word_index>` (alone) - Delete word at index
- `ECALPER` - Save and exit

## Features
- **Word-level replacement**: Change specific words without rewriting entire sentence
- **Word deletion**: Use `""` or just word_index alone to delete a word
- **Can insert periods**: Replace any word with content containing `.`
- **Interactive loop**: Make multiple changes in one session
- **Sentence lock**: Prevents concurrent editing

## Test Scenario

### 1. Create a file and add content
```
CREATE test.txt
WRITE test.txt 0
Hello World from the Network File System
ETIRW
```

### 2. Use REPLACE to modify words
```
REPLACE test.txt 0
```

**Interactive session:**
```
=== INTERACTIVE REPLACE MODE ===
Format: <word_index> <content>
Example: 3 replacement
Type 'ECALPER' to save and exit
Use "" to delete a word
==============================

edit> 3 to
✓ ACK:WORD_UPDATED
edit> 4 our
✓ ACK:WORD_UPDATED
edit> 6 ""
✓ ACK:WORD_DELETED
edit> ECALPER
ACK:REPLACE_COMPLETE All changes saved.
```

**Result:** `Hello World to our Network System`

### 3. Delete multiple words
```
REPLACE test.txt 0
edit> 5 ""
✓ ACK:WORD_DELETED
edit> 4 ""
✓ ACK:WORD_DELETED
edit> ECALPER
ACK:REPLACE_COMPLETE All changes saved.
```

**Result:** `Hello World to System`

### 4. Insert periods (for sentence splitting effect)
```
REPLACE test.txt 0
edit> 2 World.
✓ ACK:WORD_UPDATED
edit> ECALPER
ACK:REPLACE_COMPLETE All changes saved.
```

**Result:** `Hello World. to System`

### 5. Combine REPLACE with other commands
```
# Create checkpoint before major changes
CHECKPOINT test.txt before_replace

# Make changes
REPLACE test.txt 0
edit> 1 Goodbye
✓ ACK:WORD_UPDATED
edit> ECALPER

# Check difference
DIFF test.txt before_replace HEAD

# Revert if needed
REVERT test.txt before_replace
```

## Error Cases

### Sentence doesn't exist
```
REPLACE test.txt 99
```
**Expected:** `ERROR: Sentence index out of range.`

### Word index out of range
```
REPLACE test.txt 0
edit> 100 test
```
**Expected:** `ERR:400:WORD_INDEX_OUT_OF_RANGE`

### No write permission
```
REPLACE other_user_file.txt 0
```
**Expected:** `ERR:403:ACCESS_DENIED`

### Sentence locked by another user
```
# User 1:
REPLACE test.txt 0
(doesn't finish)

# User 2:
REPLACE test.txt 0
```
**Expected:** `ERR:423:SENTENCE_LOCKED_BY_ANOTHER_USER`

## Comparison with WRITE Command

| Feature | WRITE | REPLACE |
|---------|-------|---------|
| Mode | Add/insert words | Replace/delete words |
| Scope | Can create new sentences | Only modifies existing sentences |
| Use case | Building content | Editing existing content |
| Word addressing | 1-indexed | 1-indexed |
| Deletion | Not directly | Use `""` |
| Periods | Creates sentence boundaries | Can be inserted in any word |

## Notes
- Word indices are **1-indexed** (word 1, word 2, ...)
- Words are space-separated
- Requires **WRITE** permission
- Sentence must already exist
- Lock is held during entire session
- Changes are atomic (all or nothing)
- Supports UNDO command

## Tips
1. Use `READ` to see sentence content before replacing
2. Remember word indices start at 1
3. Type `ECALPER` exactly to finish (backwards of REPLACE)
4. You can make multiple changes in one session
5. Use checkpoints before major replacements
6. Delete words with `""` or just the word index alone
