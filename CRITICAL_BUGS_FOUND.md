# Critical Issues Found in Implementation

## ⚠️ HIGH PRIORITY ISSUES

### 1. **VIEW Flag Parsing - Possible Issue with "-al"**
**Location:** `NM/nm.c` line 1911-1912
```c
int show_all = (args && strchr(args, 'a') != NULL);      // Check if 'a' flag present
int show_details = (args && strchr(args, 'l') != NULL);  // Check if 'l' flag present
```

**Problem:** This uses `strchr` which searches the entire args string. 

**Test Case That Will FAIL:**
```bash
User> CREATE alarm.txt  # Has 'a' and 'l' in filename
User> VIEW alarm.txt     # Will trigger show_all and show_details!
```

**Impact:** Medium - Unlikely real scenario but technically incorrect

**Fix:**
```c
int show_all = 0;
int show_details = 0;

if (args) {
    // Only parse flags, not entire string
    if (args[0] == '-') {
        char *flag_ptr = args + 1;
        while (*flag_ptr && *flag_ptr != ' ') {
            if (*flag_ptr == 'a') show_all = 1;
            if (*flag_ptr == 'l') show_details = 1;
            flag_ptr++;
        }
    }
}
```

---

### 2. **EXEC Command - Sentence Delimiters Break Commands**
**Test Case That Will FAIL:**
```bash
User> CREATE test.txt
User> WRITE test.txt 0
edit> 0 echo
edit> 1 "Hello.
edit> 2 World"
edit> ETIRW

User> EXEC test.txt
```

**Expected Behavior:** Execute `echo "Hello. World"`

**Actual Behavior:** 
- Sentence 0: `echo "Hello.`
- Sentence 1: ` World"`
- EXEC tries to execute two separate commands, fails

**Problem:** Per spec, **every period is a delimiter**, even inside strings. This breaks EXEC for commands with periods.

**Impact:** HIGH - EXEC is worth 15 points, likely to fail multiple test cases

**Recommendation:** Document this limitation or add special handling for EXEC to treat whole file as one command.

---

### 3. **STREAM - No SS Disconnect Handling**
**Location:** `client/client.c` line ~155-200

**Test Case That Will FAIL:**
```bash
# Terminal 1: Start streaming large file
User> STREAM large_file.txt

# Terminal 2: Kill SS mid-stream
$ kill -9 <SS_PID>

# Expected: Error message shown to user
# Actual: Likely hangs or crashes
```

**Problem:** No timeout or disconnect detection in STREAM loop:
```c
while (1) {
    n = read(ss_socket, response, sizeof(response) - 1);
    if (n <= 0) break;  // Breaks but doesn't show error message!
    // ...
}
```

**Impact:** HIGH - STREAM requirement explicitly states:
> "If the storage server goes down mid-streaming, an appropriate error message should be displayed to the user."

**Fix:**
```c
while (1) {
    n = read(ss_socket, response, sizeof(response) - 1);
    if (n <= 0) {
        if (n == 0) {
            printf("\n⚠️  ERR: Storage Server disconnected during stream\n");
        } else {
            printf("\n⚠️  ERR: Connection error during stream\n");
        }
        break;
    }
    // ...
}
```

---

### 4. **DELETE During Active Operations - No File-in-Use Check**
**Test Case That Will FAIL:**
```bash
# Terminal 1:
User1> STREAM large_file.txt  # Takes 10+ seconds

# Terminal 2 (during stream):
User1> DELETE large_file.txt
# Expected: ERR:FILE_IN_USE or block until stream complete
# Actual: Likely deletes file, crashes STREAM
```

**Impact:** MEDIUM - Edge case but explicitly mentioned in requirements

**Fix:** Add reference counting or file-in-use tracking

---

### 5. **ADDACCESS - No User Validation**
**Test Case That Will FAIL:**
```bash
User1> CREATE test.txt
User1> ADDACCESS -R test.txt ghostuser_doesnt_exist
# Expected: ERR:USER_NOT_FOUND
# Actual: Likely accepts and adds to ACL
```

**Problem:** Need to check if username exists in registered clients

**Impact:** MEDIUM - ACL corruption, invalid users in permissions

**Fix:** Add user validation in `handle_addaccess_command`:
```c
// Check if target user exists
Client* target = NULL;
pthread_rwlock_rdlock(&g_nm.client_lock);
for (int i = 0; i < g_nm.client_count; i++) {
    if (strcmp(g_nm.clients[i].username, target_username) == 0) {
        target = &g_nm.clients[i];
        break;
    }
}
pthread_rwlock_unlock(&g_nm.client_lock);

if (!target) {
    send_error(client->socket_fd, ERR_NOT_FOUND, "User not found");
    return;
}
```

---

### 6. **CREATE - Path Traversal Vulnerability**
**Test Case That Will FAIL (Security Issue):**
```bash
User> CREATE ../../../etc/passwd
User> CREATE ../../../../../../../../tmp/hacked.txt
```

**Expected:** ERR:INVALID_FILENAME (path traversal blocked)

**Actual:** Depends on path handling - may create file outside storage root

**Impact:** CRITICAL SECURITY - Can write files anywhere on system

**Fix:** Validate filename doesn't contain "../" or absolute paths

---

### 7. **Very Long Filename - Buffer Overflow Risk**
**Test Case:**
```bash
User> CREATE aaaaaaaaaaaaaaaaaaaaaaaaa....[500 'a's]....aaa.txt
```

**Problem:** Most buffers are 256 bytes:
```c
char filename[256];
```

If input validation missing, could overflow.

**Impact:** MEDIUM - Crashes or security issue

**Fix:** Check filename length before copying

---

### 8. **UNDO Without History Limit**
**Test Case:**
```bash
User> CREATE test.txt
User> UNDO test.txt
User> UNDO test.txt
User> UNDO test.txt
# ... (Keep calling UNDO 1000 times)
```

**Expected:** After exhausting history, return ERR:NO_UNDO_HISTORY

**Actual:** Unknown - depends on backup implementation

**Impact:** LOW - Edge case

---

### 9. **Concurrent WRITE Lock - Race Condition Possible?**
**Test Case:**
```bash
# Two clients execute EXACTLY simultaneously:
# Terminal 1:                    # Terminal 2:
User1> WRITE test.txt 0          User2> WRITE test.txt 0
```

**Question:** Is `acquire_sentence_lock()` truly atomic?

**Verification Needed:** Check if there's a race condition window between checking lock and setting lock.

**Impact:** HIGH if present - Data corruption (worth 30 points)

---

### 10. **INFO Command - Missing Fields**
**Requirement:** Must show:
- File size
- Access rights
- Timestamps (creation, modified, accessed)
- Owner

**Test:** Check if ALL fields are present

---

## 🟡 MEDIUM PRIORITY ISSUES

### 11. **READ with Only Write Access**
Per requirements: "Write access includes read"

Verify this is implemented in ACL checks.

---

### 12. **Owner Cannot Revoke Own Access**
```bash
User> CREATE test.txt
User> REMACCESS test.txt user  # Trying to remove own access
User> READ test.txt  # Should still work (owner always has access)
```

---

### 13. **WRITE - Empty File Edge Case**
```bash
User> CREATE empty.txt
User> WRITE empty.txt 0
# Should work - create sentence 0
```

---

### 14. **Large File Performance**
Test with 10,000+ word file:
- Memory allocation issues?
- Timeout on operations?

---

## 🟢 LOW PRIORITY / CORNER CASES

### 15. **Unicode Characters**
```bash
User> WRITE test.txt 0
edit> 0 Hello世界🚀
edit> ETIRW
```

---

### 16. **Network Timeout Handling**
Block packets with `iptables` during operation - does it timeout gracefully?

---

### 17. **Maximum Concurrent Clients**
Connect 100+ clients - does thread pool handle it?

---

## ✅ IMPLEMENTATION STRENGTHS (Already Correct)

1. ✅ **Sentence delimiter handling** - Correctly treats ALL periods/!/? as delimiters per spec
2. ✅ **VIEW folder filtering** - Already fixed to exclude folders
3. ✅ **Trie + LRU Cache** - Efficient file lookups
4. ✅ **Sentence locking** - Implemented for concurrent write protection
5. ✅ **Backup creation** - `.bak` files for UNDO support
6. ✅ **ACL enforcement** - Permission checks in place
7. ✅ **Persistent metadata** - File metadata saved to disk

---

## 🎯 TESTING PRIORITY

### Must Test (High Failure Risk):
1. **STREAM SS disconnect** - Explicitly required, likely missing
2. **EXEC with sentence delimiters** - Will fail on periods in commands
3. **VIEW flag parsing** - Edge case but easy to test
4. **ADDACCESS non-existent user** - Likely not validated
5. **Path traversal in CREATE** - Security critical

### Should Test (Medium Risk):
1. **DELETE during active operation**
2. **Concurrent WRITE lock race**
3. **UNDO history limit**
4. **INFO all fields present**
5. **Very long filename**

### Nice to Test (Low Risk):
1. **Unicode handling**
2. **Large file performance**
3. **Maximum concurrent clients**
4. **Network timeout**

---

## 📋 Quick Fix Checklist

- [ ] Fix STREAM disconnect error message
- [ ] Add ADDACCESS user validation
- [ ] Fix VIEW flag parsing (or document limitation)
- [ ] Add path traversal check in CREATE
- [ ] Add filename length validation
- [ ] Document EXEC limitation with periods
- [ ] Add file-in-use tracking for DELETE
- [ ] Verify UNDO history limit
- [ ] Verify INFO shows all required fields
- [ ] Test concurrent WRITE lock atomicity

---

## 🚀 Recommendation

Focus on **STREAM disconnect handling** first - it's explicitly required and likely worth significant points. Then fix **ADDACCESS validation** and **path traversal** as they're quick wins.

The EXEC sentence delimiter issue is **by design per spec**, so document it rather than fix it. The spec says every period is a delimiter - this is the expected behavior even if it breaks some commands.
