; ModuleID = './tests/inputs/block.ll'
source_filename = "./tests/inputs/block.mc"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none)
define i32 @sut() local_unnamed_addr #0 {
entry:
  %retval.sroa.0 = alloca i8, align 4
  store i1 false, ptr %retval.sroa.0, align 4
  %retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0. = load i8, ptr %retval.sroa.0, align 4
  %retval.sroa.0.0.insert.ext = zext i8 %retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0.retval.sroa.0.0. to i32
  ret i32 %retval.sroa.0.0.insert.ext
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none) }
