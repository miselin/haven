; ModuleID = './src/cimport/cimport.ll'
source_filename = "/home/miselin/src/mattc/src/cimport/cimport.hv"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%enum.Token = type { i32, [260 x i8] }

@str = global [7 x i8] c"fp=%p\0A\00"
@str.1 = global [2 x i8] c"r\00"
@str.2 = global [20 x i8] c"opened %s -> fp=%p\0A\00"

; Function Attrs: nofree nounwind sanitize_address memory(argmem: readwrite)
declare noundef i32 @fgetc(ptr nocapture noundef) local_unnamed_addr #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @isspace(i32) local_unnamed_addr #1

; Function Attrs: nofree nounwind sanitize_address memory(argmem: readwrite)
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #0

; Function Attrs: mustprogress nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i32, i1 immarg) #2

; Function Attrs: nounwind sanitize_address memory(readwrite, inaccessiblemem: none)
define internal fastcc void @next_token(ptr nocapture writeonly sret(%enum.Token) %0, ptr %1) unnamed_addr #3 {
entry:
  %2 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @str, ptr %1), !dbg !4
  %3 = tail call i32 @fgetc(ptr %1), !dbg !10
  switch i32 %3, label %next_char.exit [
    i32 -1, label %if.stmt.then
    i32 0, label %if.stmt.then
  ], !dbg !11

next_char.exit:                                   ; preds = %entry
  %narrowing.i = trunc i32 %3 to i8, !dbg !12
  %cmp = icmp slt i8 %narrowing.i, 1, !dbg !13
  br i1 %cmp, label %if.stmt.then, label %if.stmt.end, !dbg !13

if.stmt.then:                                     ; preds = %entry, %entry, %next_char.exit
  %enum = alloca %enum.Token, align 8, !dbg !14
  store i32 0, ptr %enum, align 8, !dbg !14
  br label %return, !dbg !14

if.stmt.end:                                      ; preds = %next_char.exit
  %widening = and i32 %3, 255
  %4 = tail call i32 @isspace(i32 %widening), !dbg !16
  %tobool3.not = icmp eq i32 %4, 0, !dbg !16
  br i1 %tobool3.not, label %if.stmt.end14, label %if.stmt.then4, !dbg !16

if.stmt.then4:                                    ; preds = %if.stmt.end
  %5 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @str, ptr %1), !dbg !17
  %6 = tail call i32 @fgetc(ptr %1), !dbg !20
  switch i32 %6, label %next_char.exit10 [
    i32 -1, label %if.stmt.then8
    i32 0, label %if.stmt.then8
  ], !dbg !21

next_char.exit10:                                 ; preds = %if.stmt.then4
  %narrowing.i9 = trunc i32 %6 to i8, !dbg !22
  %cmp6 = icmp slt i8 %narrowing.i9, 1, !dbg !23
  br i1 %cmp6, label %if.stmt.then8, label %if.stmt.end14, !dbg !23

if.stmt.then8:                                    ; preds = %if.stmt.then4, %if.stmt.then4, %next_char.exit10
  %enum9 = alloca %enum.Token, align 8, !dbg !24
  store i32 0, ptr %enum9, align 8, !dbg !24
  br label %return, !dbg !24

if.stmt.end14:                                    ; preds = %next_char.exit10, %if.stmt.end
  %c.0 = phi i8 [ %narrowing.i9, %next_char.exit10 ], [ %narrowing.i, %if.stmt.end ], !dbg !26
  %widening16 = zext nneg i8 %c.0 to i32
  %isdigittmp = add nsw i32 %widening16, -48, !dbg !27
  %isdigit = icmp ult i32 %isdigittmp, 10, !dbg !27
  %sret = alloca %enum.Token, align 8, !dbg !26
  br i1 %isdigit, label %if.stmt.then18, label %if.stmt.end21, !dbg !27

if.stmt.then18:                                   ; preds = %if.stmt.end14
  store i32 1, ptr %sret, align 8, !dbg !28
  br label %return, !dbg !33

if.stmt.end21:                                    ; preds = %if.stmt.end14
  switch i8 %c.0, label %match.otherwise [
    i8 40, label %match.arm
    i8 41, label %match.arm53
    i8 123, label %match.arm57
    i8 125, label %match.arm61
    i8 91, label %match.arm65
    i8 93, label %match.arm69
    i8 59, label %match.arm73
    i8 43, label %match.arm77
    i8 45, label %match.arm81
    i8 42, label %match.arm85
    i8 47, label %match.arm89
    i8 37, label %match.arm93
    i8 38, label %match.arm97
    i8 124, label %match.arm101
    i8 94, label %match.arm105
    i8 126, label %match.arm109
    i8 33, label %match.arm113
    i8 61, label %match.arm117
    i8 60, label %match.arm121
    i8 62, label %match.arm125
    i8 63, label %match.arm129
    i8 58, label %match.arm133
    i8 46, label %match.arm137
    i8 44, label %match.arm141
    i8 34, label %match.arm145
  ], !dbg !34

match.otherwise:                                  ; preds = %if.stmt.end21
  store i32 1, ptr %sret, align 8, !dbg !35
  br label %return, !dbg !35

match.arm:                                        ; preds = %if.stmt.end21
  store i32 2, ptr %sret, align 8, !dbg !36
  br label %return, !dbg !36

match.arm53:                                      ; preds = %if.stmt.end21
  store i32 3, ptr %sret, align 8, !dbg !37
  br label %return, !dbg !37

match.arm57:                                      ; preds = %if.stmt.end21
  store i32 4, ptr %sret, align 8, !dbg !38
  br label %return, !dbg !38

match.arm61:                                      ; preds = %if.stmt.end21
  store i32 5, ptr %sret, align 8, !dbg !39
  br label %return, !dbg !39

match.arm65:                                      ; preds = %if.stmt.end21
  store i32 6, ptr %sret, align 8, !dbg !40
  br label %return, !dbg !40

match.arm69:                                      ; preds = %if.stmt.end21
  store i32 7, ptr %sret, align 8, !dbg !41
  br label %return, !dbg !41

match.arm73:                                      ; preds = %if.stmt.end21
  store i32 8, ptr %sret, align 8, !dbg !42
  br label %return, !dbg !42

match.arm77:                                      ; preds = %if.stmt.end21
  store i32 9, ptr %sret, align 8, !dbg !43
  br label %return, !dbg !43

match.arm81:                                      ; preds = %if.stmt.end21
  store i32 10, ptr %sret, align 8, !dbg !44
  br label %return, !dbg !44

match.arm85:                                      ; preds = %if.stmt.end21
  store i32 11, ptr %sret, align 8, !dbg !45
  br label %return, !dbg !45

match.arm89:                                      ; preds = %if.stmt.end21
  store i32 12, ptr %sret, align 8, !dbg !46
  br label %return, !dbg !46

match.arm93:                                      ; preds = %if.stmt.end21
  store i32 13, ptr %sret, align 8, !dbg !47
  br label %return, !dbg !47

match.arm97:                                      ; preds = %if.stmt.end21
  store i32 14, ptr %sret, align 8, !dbg !48
  br label %return, !dbg !48

match.arm101:                                     ; preds = %if.stmt.end21
  store i32 15, ptr %sret, align 8, !dbg !49
  br label %return, !dbg !49

match.arm105:                                     ; preds = %if.stmt.end21
  store i32 16, ptr %sret, align 8, !dbg !50
  br label %return, !dbg !50

match.arm109:                                     ; preds = %if.stmt.end21
  store i32 17, ptr %sret, align 8, !dbg !51
  br label %return, !dbg !51

match.arm113:                                     ; preds = %if.stmt.end21
  store i32 18, ptr %sret, align 8, !dbg !52
  br label %return, !dbg !52

match.arm117:                                     ; preds = %if.stmt.end21
  store i32 19, ptr %sret, align 8, !dbg !53
  br label %return, !dbg !53

match.arm121:                                     ; preds = %if.stmt.end21
  store i32 20, ptr %sret, align 8, !dbg !54
  br label %return, !dbg !54

match.arm125:                                     ; preds = %if.stmt.end21
  store i32 21, ptr %sret, align 8, !dbg !55
  br label %return, !dbg !55

match.arm129:                                     ; preds = %if.stmt.end21
  store i32 22, ptr %sret, align 8, !dbg !56
  br label %return, !dbg !56

match.arm133:                                     ; preds = %if.stmt.end21
  store i32 23, ptr %sret, align 8, !dbg !57
  br label %return, !dbg !57

match.arm137:                                     ; preds = %if.stmt.end21
  store i32 24, ptr %sret, align 8, !dbg !58
  br label %return, !dbg !58

match.arm141:                                     ; preds = %if.stmt.end21
  store i32 25, ptr %sret, align 8, !dbg !59
  br label %return, !dbg !59

match.arm145:                                     ; preds = %if.stmt.end21
  store i32 1, ptr %sret, align 8, !dbg !60
  br label %return, !dbg !64

return:                                           ; preds = %match.otherwise, %match.arm, %match.arm53, %match.arm57, %match.arm61, %match.arm65, %match.arm69, %match.arm73, %match.arm77, %match.arm81, %match.arm85, %match.arm89, %match.arm93, %match.arm97, %match.arm101, %match.arm105, %match.arm109, %match.arm113, %match.arm117, %match.arm121, %match.arm125, %match.arm129, %match.arm133, %match.arm137, %match.arm141, %match.arm145, %if.stmt.then, %if.stmt.then8, %if.stmt.then18
  %enum.sink = phi ptr [ %enum, %if.stmt.then ], [ %enum9, %if.stmt.then8 ], [ %sret, %if.stmt.then18 ], [ %sret, %match.otherwise ], [ %sret, %match.arm ], [ %sret, %match.arm53 ], [ %sret, %match.arm57 ], [ %sret, %match.arm61 ], [ %sret, %match.arm65 ], [ %sret, %match.arm69 ], [ %sret, %match.arm73 ], [ %sret, %match.arm77 ], [ %sret, %match.arm81 ], [ %sret, %match.arm85 ], [ %sret, %match.arm89 ], [ %sret, %match.arm93 ], [ %sret, %match.arm97 ], [ %sret, %match.arm101 ], [ %sret, %match.arm105 ], [ %sret, %match.arm109 ], [ %sret, %match.arm113 ], [ %sret, %match.arm117 ], [ %sret, %match.arm121 ], [ %sret, %match.arm125 ], [ %sret, %match.arm129 ], [ %sret, %match.arm133 ], [ %sret, %match.arm137 ], [ %sret, %match.arm141 ], [ %sret, %match.arm145 ]
  call void @llvm.memcpy.p0.p0.i32(ptr noundef nonnull align 4 dereferenceable(260) %0, ptr noundef nonnull align 8 dereferenceable(260) %enum.sink, i32 260, i1 false), !dbg !26
  ret void, !dbg !64
}

; Function Attrs: nofree nounwind sanitize_address memory(argmem: readwrite)
declare noalias noundef ptr @fopen(ptr nocapture noundef readonly, ptr nocapture noundef readonly) local_unnamed_addr #0

; Function Attrs: nofree nounwind sanitize_address memory(argmem: readwrite)
declare noundef i32 @fclose(ptr nocapture noundef) local_unnamed_addr #0

; Function Attrs: mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none)
define noundef i32 @haven_cimport_present() local_unnamed_addr #4 {
entry:
  ret i32 1, !dbg !65
}

; Function Attrs: nounwind sanitize_address memory(readwrite, inaccessiblemem: none)
define noundef i32 @haven_cimport_process(ptr %0) local_unnamed_addr #3 {
entry:
  %1 = tail call ptr @fopen(ptr %0, ptr nonnull @str.1), !dbg !68
  %2 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @str.2, ptr %0, ptr %1), !dbg !71
  %tok = alloca %enum.Token, align 8, !dbg !72
  call fastcc void @next_token(ptr nonnull sret(%enum.Token) %tok, ptr %1), !dbg !73
  %3 = tail call i32 @fclose(ptr %1), !dbg !74
  ret i32 -1, !dbg !75
}

attributes #0 = { nofree nounwind sanitize_address memory(argmem: readwrite) }
attributes #1 = { nounwind sanitize_address memory(argmem: readwrite) }
attributes #2 = { mustprogress nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #3 = { nounwind sanitize_address memory(readwrite, inaccessiblemem: none) }
attributes #4 = { mustprogress nofree norecurse nosync nounwind sanitize_address willreturn memory(none) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "haven", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "cimport.hv", directory: "/home/miselin/src/mattc/src/cimport")
!2 = !{i32 4, !"Debug Info Version", i32 3}
!3 = !{i32 4, !"Dwarf Version", i32 2}
!4 = !DILocation(line: 63, column: 25, scope: !5, inlinedAt: !7)
!5 = distinct !DILexicalBlock(scope: !6, file: !1, line: 62, column: 35)
!6 = distinct !DISubprogram(name: "next_char", scope: null, file: !1, line: 62, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!7 = distinct !DILocation(line: 73, column: 29, scope: !8)
!8 = distinct !DILexicalBlock(scope: !9, file: !1, line: 72, column: 39)
!9 = distinct !DISubprogram(name: "next_token", scope: null, file: !1, line: 72, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!10 = !DILocation(line: 64, column: 25, scope: !5, inlinedAt: !7)
!11 = !DILocation(line: 66, column: 10, scope: !5, inlinedAt: !7)
!12 = !DILocation(line: 68, column: 21, scope: !5, inlinedAt: !7)
!13 = !DILocation(line: 74, column: 12, scope: !8)
!14 = !DILocation(line: 75, column: 12, scope: !15)
!15 = distinct !DILexicalBlock(scope: !8, file: !1, line: 74, column: 16)
!16 = !DILocation(line: 78, column: 24, scope: !8)
!17 = !DILocation(line: 63, column: 25, scope: !5, inlinedAt: !18)
!18 = distinct !DILocation(line: 79, column: 25, scope: !19)
!19 = distinct !DILexicalBlock(scope: !8, file: !1, line: 78, column: 27)
!20 = !DILocation(line: 64, column: 25, scope: !5, inlinedAt: !18)
!21 = !DILocation(line: 66, column: 10, scope: !5, inlinedAt: !18)
!22 = !DILocation(line: 68, column: 21, scope: !5, inlinedAt: !18)
!23 = !DILocation(line: 80, column: 16, scope: !19)
!24 = !DILocation(line: 81, column: 16, scope: !25)
!25 = distinct !DILexicalBlock(scope: !19, file: !1, line: 80, column: 20)
!26 = !DILocation(line: 0, scope: !8)
!27 = !DILocation(line: 85, column: 24, scope: !8)
!28 = !DILocation(line: 55, column: 10, scope: !29, inlinedAt: !31)
!29 = distinct !DILexicalBlock(scope: !30, file: !1, line: 54, column: 52)
!30 = distinct !DISubprogram(name: "integer_literal", scope: null, file: !1, line: 54, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!31 = distinct !DILocation(line: 86, column: 34, scope: !32)
!32 = distinct !DILexicalBlock(scope: !8, file: !1, line: 85, column: 27)
!33 = !DILocation(line: 86, column: 34, scope: !32)
!34 = !DILocation(line: 90, column: 12, scope: !8)
!35 = !DILocation(line: 115, column: 13, scope: !8)
!36 = !DILocation(line: 90, column: 15, scope: !8)
!37 = !DILocation(line: 91, column: 15, scope: !8)
!38 = !DILocation(line: 92, column: 15, scope: !8)
!39 = !DILocation(line: 93, column: 15, scope: !8)
!40 = !DILocation(line: 94, column: 15, scope: !8)
!41 = !DILocation(line: 95, column: 15, scope: !8)
!42 = !DILocation(line: 96, column: 15, scope: !8)
!43 = !DILocation(line: 97, column: 15, scope: !8)
!44 = !DILocation(line: 98, column: 15, scope: !8)
!45 = !DILocation(line: 99, column: 15, scope: !8)
!46 = !DILocation(line: 100, column: 15, scope: !8)
!47 = !DILocation(line: 101, column: 15, scope: !8)
!48 = !DILocation(line: 102, column: 15, scope: !8)
!49 = !DILocation(line: 103, column: 15, scope: !8)
!50 = !DILocation(line: 104, column: 15, scope: !8)
!51 = !DILocation(line: 105, column: 15, scope: !8)
!52 = !DILocation(line: 106, column: 15, scope: !8)
!53 = !DILocation(line: 107, column: 15, scope: !8)
!54 = !DILocation(line: 108, column: 15, scope: !8)
!55 = !DILocation(line: 109, column: 15, scope: !8)
!56 = !DILocation(line: 110, column: 15, scope: !8)
!57 = !DILocation(line: 111, column: 15, scope: !8)
!58 = !DILocation(line: 112, column: 15, scope: !8)
!59 = !DILocation(line: 113, column: 15, scope: !8)
!60 = !DILocation(line: 51, column: 10, scope: !61, inlinedAt: !63)
!61 = distinct !DILexicalBlock(scope: !62, file: !1, line: 50, column: 36)
!62 = distinct !DISubprogram(name: "string_literal", scope: null, file: !1, line: 50, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!63 = distinct !DILocation(line: 114, column: 33, scope: !8)
!64 = !DILocation(line: 114, column: 33, scope: !8)
!65 = !DILocation(line: 18, column: 6, scope: !66)
!66 = distinct !DILexicalBlock(scope: !67, file: !1, line: 17, column: 37)
!67 = distinct !DISubprogram(name: "haven_cimport_present", scope: null, file: !1, line: 17, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!68 = !DILocation(line: 22, column: 33, scope: !69)
!69 = distinct !DILexicalBlock(scope: !70, file: !1, line: 21, column: 56)
!70 = distinct !DISubprogram(name: "haven_cimport_process", scope: null, file: !1, line: 21, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!71 = !DILocation(line: 23, column: 48, scope: !69)
!72 = !DILocation(line: 25, column: 8, scope: !69)
!73 = !DILocation(line: 25, column: 28, scope: !69)
!74 = !DILocation(line: 27, column: 14, scope: !69)
!75 = !DILocation(line: 29, column: 6, scope: !69)
