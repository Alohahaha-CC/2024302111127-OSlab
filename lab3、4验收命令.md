
cd /mnt/e/作业/大三作业/OSlab/os-kernel
make qemu

| 阶段 | 命令            | 看什么                                                    | 归哪个 lab |
| ---- | --------------- | --------------------------------------------------------- | ---------- |
| lab3 | （启动自动）    | dump 页表结构                                             | lab3       |
| lab3 | `hi`          | `hi: user program running, pid=2`                       | lab2/lab3  |
| lab3 | `badaccess`   | `TEST-3a PASS` + `TEST-3b PASS`                       | lab3       |
| lab3 | `memcap`      | `TEST-4 PASS: OOM returned error after ... chunks`      | lab3       |
| lab3 | `selftest3`   | `SELFTEST-3a PASS` + `SELFTEST-3b PASS`               | lab3       |
| lab4 | `alt`         | A/B **交错**输出，最后 `alt done`                 | lab4       |
| lab4 | `bench`       | 两行 `RESULT ...` + `PASS: bench`                     | lab4       |
| lab4 | `stride_test` | `RESULT stride_ratio = a:b`（b/a≈3）+ `PASS: stride` | lab4       |
