; ModuleID = './tests/inputs/defer.ll'
source_filename = "./tests/inputs/defer.mc"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @sut_exit(i32) local_unnamed_addr #0

; Function Attrs: nofree nosync nounwind sanitize_address memory(none)
define noundef i32 @sut() local_unnamed_addr #1 {
entry:
  %0 = tail call i32 @sut_exit(i32 1)
  ret i32 0
}

attributes #0 = { nounwind sanitize_address memory(argmem: readwrite) }
attributes #1 = { nofree nosync nounwind sanitize_address memory(none) }
