# Comprehensive Test Cases for Client Functionalities
## Tests That May Expose Implementation Issues

---

## 1. VIEW Command [10 points]

### Test Case 1.1: VIEW with no flags
```bash
# Setup: Create files owned by different users
User1> CREATE file1.txt
User2> CREATE file2.txt
User2> ADDACCESS -R file2.txt user1

# Test
User1> VIEW
# Expected: Lists file1.txt and file2.txt (has read access)
# Bug risk: If not filtering by access properly, may show files without permission
```

### Test Case 1.2: VIEW -a (all files)
```bash
User1> VIEW -a
# Expected: Lists ALL files in system regardless of access
# Bug risk: May still filter by access (not showing truly all files)
```

### Test Case 1.3: VIEW -l (with details)
```bash
User1> VIEW -l
# Expected: Shows word count, character count, last access, owner for accessible files
# Bug risk: Missing fields (word/char count calculation), incorrect timestamps
```

### Test Case 1.4: VIEW -al (combined flags)
```bash
User1> VIEW -al
# Expected: All files with full details
# Bug risk: 
# - Flag parsing fails (reads as two separate flags)
# - Shows partial data or filters incorrectly
```

### Test Case 1.5: VIEW with folders in system
```bash
User1> CREATEFOLDER docs
User1> CREATE docs/file1.txt
User1> VIEW
# Expected: Should NOT show "docs" folder in VIEW output
# Bug risk: Folders appearing in file list (already fixed but verify)
```

---

## 2. READ Command [10 points]

### Test Case 2.1: READ without access
```bash
User1> CREATE private.txt
User1> WRITE private.txt 0
edit> 0 Secret
edit> ETIRW

User2> READ private.txt
# Expected: ERR:403 ACCESS_DENIED
# Bug risk: Allows reading without permission check
```

### Test Case 2.2: READ with only write access
```bash
User1> CREATE test.txt
User1> ADDACCESS -W test.txt user2

User2> READ test.txt
# Expected: Should succeed (write access includes read)
# Bug risk: Denies read even though write access granted
```

### Test Case 2.3: READ during concurrent WRITE
```bash
# User1 terminal:
User1> WRITE test.txt 0
edit> 0 Hello

# User2 terminal (while User1 still in edit mode):
User2> READ test.txt
# Expected: Should read current content, not crash
# Bug risk: Deadlock, race condition, or incorrect data
```

### Test Case 2.4: READ empty file
```bash
User1> CREATE empty.txt
User1> READ empty.txt
# Expected: Empty output or "File is empty"
# Bug risk: Crashes, shows garbage data, or EOF handling error
```

---

## 3. CREATE Command [10 points]

### Test Case 3.1: CREATE duplicate filename
```bash
User1> CREATE test.txt
User1> CREATE test.txt
# Expected: ERR:409 FILE_EXISTS or similar
# Bug risk: Overwrites existing file or creates duplicate entry
```

### Test Case 3.2: CREATE with path separator in name
```bash
User1> CREATE ../etc/passwd
# Expected: ERR:INVALID_FILENAME (path traversal prevention)
# Bug risk: Creates file outside storage root, security vulnerability
```

### Test Case 3.3: CREATE in non-existent folder
```bash
User1> CREATE nonexistent/file.txt
# Expected: ERR:404 FOLDER_NOT_FOUND
# Bug risk: Creates file at root or crashes
```

### Test Case 3.4: CREATE with very long filename
```bash
User1> CREATE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.txt
# Expected: ERR:INVALID_FILENAME (filename too long)
# Bug risk: Buffer overflow, truncation without error
```

---

## 4. WRITE Command [30 points]

### Test Case 4.1: Concurrent WRITE same sentence
```bash
# Terminal 1:
User1> WRITE test.txt 0
edit> 0 Hello

# Terminal 2 (simultaneously):
User2> WRITE test.txt 0
# Expected: ERR:423 SENTENCE_LOCKED
# Bug risk: Both acquire lock, data corruption
```

### Test Case 4.2: Sentence delimiter handling - period in middle
```bash
User1> WRITE test.txt 0
edit> 0 e.g.
edit> 1 this
edit> 2 is
edit> 3 a
edit> 4 test.
edit> ETIRW

User1> READ test.txt
# Expected: Sentence 0: "e.g. this is a test."
#           Sentence 1: " is a test."  (period after "e.g." creates new sentence)
# Bug risk: Doesn't split on mid-word periods
```

### Test Case 4.3: Multiple delimiters
```bash
User1> WRITE test.txt 0
edit> 0 Hello!
edit> 1 How
edit> 2 are
edit> 3 you?
edit> 4 I'm
edit> 5 fine.
edit> ETIRW

User1> READ test.txt
# Expected: 3 sentences split by !, ?, .
# Bug risk: Doesn't recognize ! or ? as delimiters
```

### Test Case 4.4: WRITE sentence out of bounds
```bash
User1> CREATE new.txt
User1> WRITE new.txt 100
# Expected: ERR:INVALID_SENTENCE (sentence doesn't exist yet)
# Bug risk: Crashes, creates 100 empty sentences
```

### Test Case 4.5: Sentence index update after concurrent writes
```bash
# User1 writes sentence creating delimiter
User1> WRITE test.txt 0
edit> 0 First.
edit> 1 Second
edit> ETIRW

# Now sentence indices: 0="First." 1=" Second"
# User2 tries to write sentence 1
User2> WRITE test.txt 1
edit> 0 Modified
edit> ETIRW

User2> READ test.txt
# Expected: "First. Modified"
# Bug risk: Sentence indices not updated, writes to wrong location
```

### Test Case 4.6: WRITE without ACCESS
```bash
User1> CREATE private.txt
User2> WRITE private.txt 0
# Expected: ERR:403 ACCESS_DENIED (before locking sentence)
# Bug risk: Locks sentence then checks permission
```

### Test Case 4.7: Special characters in content
```bash
User1> WRITE test.txt 0
edit> 0 Hello\nWorld
edit> 1 Tab\there
edit> 2 "Quotes"
edit> ETIRW
# Expected: Should handle escapes correctly
# Bug risk: Parsing breaks on special chars
```

---

## 5. UNDO Command [15 points]

### Test Case 5.1: UNDO without permission
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 Original
edit> ETIRW

User2> UNDO test.txt
# Expected: ERR:403 ACCESS_DENIED (need write access to undo)
# Bug risk: Allows anyone to undo
```

### Test Case 5.2: Multiple UNDO
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 Version1
edit> ETIRW

User1> WRITE test.txt 0
edit> 0 Version2
edit> ETIRW

User1> WRITE test.txt 0
edit> 0 Version3
edit> ETIRW

User1> UNDO test.txt  # -> Version2
User1> UNDO test.txt  # -> Version1
User1> UNDO test.txt  # -> Should fail (no more history)
# Expected: ERR:NO_HISTORY
# Bug risk: Undo limit not tracked, crashes
```

### Test Case 5.3: UNDO after file modification by different user
```bash
User1> CREATE shared.txt
User1> ADDACCESS -W shared.txt user2
User1> WRITE shared.txt 0
edit> 0 UserOne
edit> ETIRW

User2> WRITE shared.txt 0
edit> 0 UserTwo
edit> ETIRW

User2> UNDO shared.txt
# Expected: Reverts to "UserOne" (file-specific undo, not user-specific)
# Bug risk: Only tracks per-user undo history
```

### Test Case 5.4: UNDO on non-existent file
```bash
User1> UNDO ghost.txt
# Expected: ERR:404 FILE_NOT_FOUND
# Bug risk: Crashes or creates file
```

---

## 6. INFO Command [10 points]

### Test Case 6.1: INFO shows all required fields
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 Hello world
edit> ETIRW

User1> INFO test.txt
# Expected output should include:
# - File size (bytes)
# - Access rights (owner: RW)
# - Creation timestamp
# - Last modified timestamp
# - Last access timestamp
# - Owner username
# Bug risk: Missing any of these fields
```

### Test Case 6.2: INFO without access
```bash
User1> CREATE private.txt
User2> INFO private.txt
# Expected: ERR:403 or limited info (depends on spec)
# Bug risk: Shows full details to unauthorized user
```

### Test Case 6.3: INFO with multiple users having access
```bash
User1> CREATE shared.txt
User1> ADDACCESS -R shared.txt user2
User1> ADDACCESS -W shared.txt user3

User1> INFO shared.txt
# Expected: Shows all access rights (user2:R, user3:RW)
# Bug risk: Incomplete ACL display
```

---

## 7. DELETE Command [10 points]

### Test Case 7.1: DELETE by non-owner
```bash
User1> CREATE test.txt
User1> ADDACCESS -W test.txt user2

User2> DELETE test.txt
# Expected: ERR:403 PERMISSION_DENIED (only owner can delete)
# Bug risk: Anyone with write access can delete
```

### Test Case 7.2: DELETE updates ACL and metadata
```bash
User1> CREATE test.txt
User1> ADDACCESS -R test.txt user2

User1> DELETE test.txt

# Check metadata file still has references
User1> VIEW
# Expected: test.txt should NOT appear
# Bug risk: Metadata not updated, orphaned entries
```

### Test Case 7.3: DELETE file being read by another user
```bash
# Terminal 1:
User1> STREAM test.txt  # Long operation

# Terminal 2 (during stream):
User1> DELETE test.txt
# Expected: Should either block or deny with ERR:FILE_IN_USE
# Bug risk: Deletes while SS serving it, crashes STREAM operation
```

### Test Case 7.4: DELETE non-existent file
```bash
User1> DELETE ghost.txt
# Expected: ERR:404 FILE_NOT_FOUND
# Bug risk: Crashes
```

---

## 8. STREAM Command [15 points]

### Test Case 8.1: STREAM with 0.1s delay verification
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 word1
edit> 1 word2
edit> 2 word3
edit> 3 word4
edit> 4 word5
edit> ETIRW

User1> STREAM test.txt
# Expected: Each word appears with ~0.1 second delay
# Bug risk: No delay, or wrong delay timing
# Manual verification: Use stopwatch, 5 words should take ~0.5 seconds
```

### Test Case 8.2: STREAM when SS goes down mid-stream
```bash
User1> CREATE large.txt
# (Create file with 100+ words)

# Terminal 1:
User1> STREAM large.txt

# Terminal 2 (after 20 words displayed):
$ kill -9 <SS_PID>

# Expected: Error message "ERR: Storage Server disconnected during stream"
# Bug risk: Hangs indefinitely, crashes client, no error message
```

### Test Case 8.3: STREAM without read access
```bash
User1> CREATE private.txt
User2> STREAM private.txt
# Expected: ERR:403 ACCESS_DENIED
# Bug risk: Permission not checked, leaks content
```

### Test Case 8.4: STREAM empty file
```bash
User1> CREATE empty.txt
User1> STREAM empty.txt
# Expected: Completes immediately or "No content"
# Bug risk: Hangs or crashes
```

### Test Case 8.5: Direct SS connection
```bash
User1> STREAM test.txt
# Expected: Client establishes DIRECT connection to SS (not through NM)
# Verification: Check tcpdump/netstat - should see client<->SS connection
# Bug risk: Routing through NM (performance issue, not spec compliant)
```

---

## 9. LIST Command [10 points]

### Test Case 9.1: LIST all users
```bash
# Register 3 users
User1> LIST
# Expected: Shows all registered usernames (user1, user2, user3)
# Bug risk: Shows only active users, or missing some
```

### Test Case 9.2: LIST shows users who disconnected
```bash
# Register user1, user2, user3
# user2 disconnects (QUIT)

User1> LIST
# Expected: Should show all users or indicate active/inactive status
# Bug risk: Removes disconnected users from list
```

---

## 10. Access Control [15 points]

### Test Case 10.1: ADDACCESS by non-owner
```bash
User1> CREATE test.txt
User2> ADDACCESS -R test.txt user3
# Expected: ERR:403 PERMISSION_DENIED (only owner can grant access)
# Bug risk: Anyone can grant access
```

### Test Case 10.2: ADDACCESS -R then ADDACCESS -W
```bash
User1> CREATE test.txt
User1> ADDACCESS -R test.txt user2

User2> READ test.txt   # Should work
User2> WRITE test.txt 0  # Should fail (only read access)

User1> ADDACCESS -W test.txt user2

User2> WRITE test.txt 0  # Should now work
# Expected: Write access upgrade works
# Bug risk: Still blocks write, or ACL not updated
```

### Test Case 10.3: REMACCESS removes all access
```bash
User1> CREATE test.txt
User1> ADDACCESS -W test.txt user2

User2> READ test.txt   # Works

User1> REMACCESS test.txt user2

User2> READ test.txt   # Should fail
User2> WRITE test.txt 0  # Should fail
# Expected: Both read and write denied after REMACCESS
# Bug risk: Only removes read or write, not both
```

### Test Case 10.4: Owner always has access
```bash
User1> CREATE test.txt
User1> REMACCESS test.txt user1  # Try to remove own access

User1> READ test.txt
User1> WRITE test.txt 0
# Expected: Owner retains full access (can't revoke own access)
# Bug risk: Owner loses access, locked out of own file
```

### Test Case 10.5: ADDACCESS to non-existent user
```bash
User1> CREATE test.txt
User1> ADDACCESS -R test.txt ghostuser
# Expected: ERR:USER_NOT_FOUND
# Bug risk: Accepts non-existent user, ACL corrupted
```

---

## 11. EXEC Command [15 points]

### Test Case 11.1: EXEC runs on Name Server
```bash
User1> CREATE script.txt
User1> WRITE script.txt 0
edit> 0 hostname
edit> ETIRW

User1> EXEC script.txt
# Expected: Shows NM hostname (not SS or client hostname)
# Bug risk: Executes on wrong machine (SS or client)
```

### Test Case 11.2: EXEC without read access
```bash
User1> CREATE script.txt
User2> EXEC script.txt
# Expected: ERR:403 ACCESS_DENIED
# Bug risk: Executes without permission check
```

### Test Case 11.3: EXEC with multi-line commands
```bash
User1> CREATE commands.txt
User1> WRITE commands.txt 0
edit> 0 echo
edit> 1 "Hello"
edit> 2 ls
edit> 3 -la
edit> 4 pwd
edit> ETIRW

User1> EXEC commands.txt
# Expected: Executes all commands, outputs returned to client
# Bug risk: 
# - Only executes first command
# - Sentence delimiters break command parsing
# - Output not piped correctly
```

### Test Case 11.4: EXEC with malicious commands
```bash
User1> CREATE malicious.txt
User1> WRITE malicious.txt 0
edit> 0 rm
edit> 1 -rf
edit> 2 /
edit> ETIRW

User1> EXEC malicious.txt
# Expected: Should execute (security risk but per spec)
# Verification: Ensure NM is sandboxed or this is documented
# Bug risk: Command injection, NM compromise
```

### Test Case 11.5: EXEC empty file
```bash
User1> CREATE empty.txt
User1> EXEC empty.txt
# Expected: No commands executed, success message
# Bug risk: Crashes
```

### Test Case 11.6: EXEC with sentence delimiters in command
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 echo
edit> 1 "Hello.
edit> 2 World!"
edit> ETIRW

User1> EXEC test.txt
# Expected: Should handle periods and ! as sentence delimiters
# Bug risk: Command broken into multiple sentences, execution fails
```

---

## 12. Edge Cases & Stress Tests

### Test Case 12.1: Rapid command execution
```bash
# Send 100 commands in rapid succession
for i in {1..100}; do
  CREATE file$i.txt
done
# Expected: All files created, no race conditions
# Bug risk: Commands lost, race conditions, crashes
```

### Test Case 12.2: Maximum concurrent connections
```bash
# Connect 100 clients simultaneously
# Each performs operations
# Expected: All handle correctly
# Bug risk: Connection limit, thread pool exhaustion
```

### Test Case 12.3: Very large file
```bash
User1> CREATE huge.txt
# Write 10,000 sentences with 100 words each
User1> READ huge.txt
User1> STREAM huge.txt
# Expected: Handles without memory issues
# Bug risk: Memory overflow, timeout, crashes
```

### Test Case 12.4: Unicode and special characters
```bash
User1> CREATE test.txt
User1> WRITE test.txt 0
edit> 0 Hello世界
edit> 1 🚀
edit> 2 café
edit> ETIRW

User1> READ test.txt
# Expected: Preserves Unicode characters
# Bug risk: Corrupted output, encoding issues
```

### Test Case 12.5: Network partition during operation
```bash
# Block NM<->SS communication during file operation
User1> READ large_file.txt
# (Use iptables to drop packets)
# Expected: Timeout with appropriate error
# Bug risk: Hangs indefinitely
```

---

## Summary of High-Risk Areas

### Critical Issues Likely to Fail:
1. **VIEW -al flag parsing** - Combined flag handling
2. **WRITE sentence delimiter handling** - e.g., "e.g." creates extra sentences
3. **Concurrent WRITE locking** - Race condition on sentence lock
4. **STREAM SS disconnect** - No error handling when SS dies mid-stream
5. **EXEC command parsing** - Sentence delimiters breaking commands
6. **UNDO history depth** - No limit checking
7. **DELETE during active STREAM** - File in use not checked
8. **ADDACCESS non-existent user** - No user validation
9. **CREATE path traversal** - Security vulnerability
10. **Very long filename** - Buffer overflow risk

### Performance Issues:
1. **Large file handling** - Memory allocation for 10k+ sentences
2. **Concurrent client scaling** - Thread pool limits
3. **STREAM timing** - 0.1s delay accuracy

### Security Issues:
1. **EXEC arbitrary commands** - Can compromise NM
2. **Path traversal in CREATE** - Can write outside storage root
3. **Permission checks ordering** - Lock before access check

---

## Testing Strategy

### Phase 1: Basic Functionality (Days 1-2)
- Test each command individually
- Verify success cases work

### Phase 2: Access Control (Day 3)
- Test permission denied scenarios
- Verify ACL updates correctly

### Phase 3: Concurrency (Days 4-5)
- Multiple clients, simultaneous operations
- Lock testing, race conditions

### Phase 4: Edge Cases (Day 6)
- Large files, special characters
- Error conditions, network failures

### Phase 5: Stress Testing (Day 7)
- 100+ concurrent clients
- Rapid command execution
- Memory/resource exhaustion

---

## Automated Test Script Template

```bash
#!/bin/bash
# Automated test runner

# Start NM and SS
./name_server &
NM_PID=$!
sleep 2
./storage_server 127.0.0.1 9001 9002 &
SS_PID=$!
sleep 2

# Run tests
echo "Running test suite..."

# Test VIEW
expect <<EOF
spawn ./client
expect "username:"
send "user1\r"
expect "nfs:user1>"
send "CREATE test.txt\r"
expect "nfs:user1>"
send "VIEW\r"
expect "test.txt"
expect "nfs:user1>"
send "QUIT\r"
EOF

# Cleanup
kill $NM_PID $SS_PID
```

---

## Grading Rubric Alignment

Each test case maps to specific rubric points:
- **VIEW**: 10 pts → TC 1.1-1.5
- **READ**: 10 pts → TC 2.1-2.4
- **CREATE**: 10 pts → TC 3.1-3.4
- **WRITE**: 30 pts → TC 4.1-4.7
- **UNDO**: 15 pts → TC 5.1-5.4
- **INFO**: 10 pts → TC 6.1-6.3
- **DELETE**: 10 pts → TC 7.1-7.4
- **STREAM**: 15 pts → TC 8.1-8.5
- **LIST**: 10 pts → TC 9.1-9.2
- **Access Control**: 15 pts → TC 10.1-10.5
- **EXEC**: 15 pts → TC 11.1-11.6

**Total: 150 points**

Good luck! 🚀
