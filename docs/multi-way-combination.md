# Multi-way combination attacks

The extended combination modes concatenate one word from each wordlist, in command-line order:

| Attack mode | Candidate layout |
| --- | --- |
| `-a 11` | `word1 + word2 + word3` |
| `-a 12` | `word1 + word2 + word3 + word4` |
| `-a 13` | `word1 + word2 + word3 + word4 + word5` |
| `-a 14` | `word1 + word2 + word3 + word4 + word5 + word6` |

Windows examples:

```powershell
.\hashcat.exe -m 0 -a 13 hashes.txt words1.txt words2.txt words3.txt words4.txt words5.txt
.\hashcat.exe -m 0 -a 14 hashes.txt words1.txt words2.txt words3.txt words4.txt words5.txt words6.txt
```

For every mode, the first `N - 1` words are concatenated by Hashcat's pipelined CPU candidate producer. The final wordlist is processed by the GPU combinator kernel. The total candidate count is the product of all input wordlist counts; `--keyspace` reports the base product and `--total-candidates` reports the full product.

The `--slow-candidates` (`-S`), brain-client, and `--stdout` paths are not supported for modes 11 through 14. Normal GPU execution, `--keyspace`, `--total-candidates`, restore/status accounting, and potfiles use the standard Hashcat paths.

Candidates longer than the selected hash mode's maximum supported password length are rejected in the same way as the existing multi-way modes.
