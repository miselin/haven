; ModuleID = 'tests/inputs/block.ll'
source_filename = "tests/inputs/block.hv"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none)
define noundef i32 @sut() local_unnamed_addr #0 {
entry:
  ret i32 0
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none) }
