; ModuleID = './tests/inputs/defer.mc'
source_filename = "./tests/inputs/defer.mc"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @sut_exit(i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define i32 @sut() #0 {
entry:
  %retval = alloca i32, align 4, !dbg !2
  store i32 0, ptr %retval, align 4, !dbg !4
  br label %defers, !dbg !4

defers:                                           ; preds = %entry
  %0 = call i32 @sut_exit(i32 1), !dbg !6
  br label %return, !dbg !6

return:                                           ; preds = %defers
  %1 = load i32, ptr %retval, align 4, !dbg !6
  ret i32 %1, !dbg !6
}

attributes #0 = { nounwind sanitize_address memory(argmem: readwrite) }

!llvm.dbg.cu = !{!0}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "mattc", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "./tests/inputs/defer.mc", directory: "examples")
!2 = !DILocation(line: 3, column: 4, scope: !3)
!3 = distinct !DISubprogram(name: "sut", scope: null, file: !1, line: 3, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!4 = !DILocation(line: 6, column: 6, scope: !5)
!5 = distinct !DILexicalBlock(scope: !3, file: !1, line: 3, column: 19)
!6 = !DILocation(line: 4, column: 21, scope: !3)
