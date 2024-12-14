; ModuleID = 'tests/inputs/block.hv'
source_filename = "tests/inputs/block.hv"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define i32 @sut() #0 {
entry:
  %retval = alloca i32, align 4, !dbg !2
  store i32 0, ptr %retval, align 4, !dbg !4
  br label %defers, !dbg !4

defers:                                           ; preds = %entry
  br label %return, !dbg !4

return:                                           ; preds = %defers
  %0 = load i32, ptr %retval, align 4, !dbg !4
  ret i32 %0, !dbg !4
}

attributes #0 = { nounwind sanitize_address memory(argmem: readwrite) }

!llvm.dbg.cu = !{!0}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "haven", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "tests/inputs/block.hv", directory: "examples")
!2 = !DILocation(line: 1, column: 4, scope: !3)
!3 = distinct !DISubprogram(name: "sut", scope: null, file: !1, line: 1, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!4 = !DILocation(line: 4, column: 14, scope: !5)
!5 = distinct !DILexicalBlock(scope: !6, file: !1, line: 3, column: 10)
!6 = distinct !DILexicalBlock(scope: !7, file: !1, line: 2, column: 6)
!7 = distinct !DILexicalBlock(scope: !3, file: !1, line: 1, column: 19)
