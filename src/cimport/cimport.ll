; ModuleID = '/home/miselin/src/mattc/src/cimport/cimport.hv'
source_filename = "/home/miselin/src/mattc/src/cimport/cimport.hv"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%enum.Token = type { i32, [260 x i8] }

@str = global [7 x i8] c"fp=%p\0A\00"
@str.1 = global [2 x i8] c"r\00"
@str.2 = global [20 x i8] c"opened %s -> fp=%p\0A\00"

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @fgetc(ptr) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @ungetc(i32, ptr) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @read(i32, ptr, i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @isdigit(i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @isspace(i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @printf(ptr, ...) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define internal void @string_literal(ptr sret(%enum.Token) %0, ptr %1) #0 {
entry:
  %enum = alloca %enum.Token, align 8, !dbg !4
  %tag = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 0, !dbg !4
  %buf = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 1, !dbg !4
  store i32 1, ptr %tag, align 4, !dbg !4
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %enum, i32 260, i1 false), !dbg !4
  br label %defers, !dbg !4

defers:                                           ; preds = %entry
  br label %return, !dbg !4

return:                                           ; preds = %defers
  ret void, !dbg !4
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i32, i1 immarg) #1

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define internal void @integer_literal(ptr sret(%enum.Token) %0, ptr %1, i8 %2) #0 {
entry:
  %enum = alloca %enum.Token, align 8, !dbg !7
  %tag = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 0, !dbg !7
  %buf = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 1, !dbg !7
  store i32 1, ptr %tag, align 4, !dbg !7
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %enum, i32 260, i1 false), !dbg !7
  br label %defers, !dbg !7

defers:                                           ; preds = %entry
  br label %return, !dbg !7

return:                                           ; preds = %defers
  ret void, !dbg !7
}

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define internal void @identifier(ptr sret(%enum.Token) %0, ptr %1, i8 %2) #0 {
entry:
  %enum = alloca %enum.Token, align 8, !dbg !10
  %tag = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 0, !dbg !10
  %buf = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 1, !dbg !10
  store i32 1, ptr %tag, align 4, !dbg !10
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %enum, i32 260, i1 false), !dbg !10
  br label %defers, !dbg !10

defers:                                           ; preds = %entry
  br label %return, !dbg !10

return:                                           ; preds = %defers
  ret void, !dbg !10
}

; Function Attrs: nounwind sanitize_address memory(readwrite)
define internal i8 @next_char(ptr %0) #2 {
entry:
  %retval = alloca i8, align 1, !dbg !13
  %1 = call i32 (ptr, ...) @printf(ptr @str, ptr %0), !dbg !15
  %n = alloca i32, align 4, !dbg !17
  %2 = call i32 @fgetc(ptr %0), !dbg !18
  store i32 %2, ptr %n, align 4, !dbg !18
  %n1 = load i32, ptr %n, align 4, !dbg !19
  %comp = icmp eq i32 %n1, -1, !dbg !20
  br i1 %comp, label %match.arm, label %3, !dbg !20

3:                                                ; preds = %entry
  %comp2 = icmp eq i32 %n1, 0, !dbg !21
  br i1 %comp2, label %match.arm4, label %match.otherwise, !dbg !21

match.otherwise:                                  ; preds = %3
  %n3 = load i32, ptr %n, align 4, !dbg !22
  %narrowing = trunc i32 %n3 to i8, !dbg !22
  br label %match.post, !dbg !22

match.arm:                                        ; preds = %entry
  br label %match.post, !dbg !23

match.arm4:                                       ; preds = %3
  br label %match.post, !dbg !24

match.post:                                       ; preds = %match.arm4, %match.arm, %match.otherwise
  %match.result = phi i8 [ %narrowing, %match.otherwise ], [ -1, %match.arm ], [ -1, %match.arm4 ], !dbg !19
  store i8 %match.result, ptr %retval, align 1, !dbg !24
  br label %defers, !dbg !24

defers:                                           ; preds = %match.post
  br label %return, !dbg !24

return:                                           ; preds = %defers
  %4 = load i8, ptr %retval, align 1, !dbg !24
  ret i8 %4, !dbg !24
}

; Function Attrs: nounwind sanitize_address memory(readwrite)
define internal void @next_token(ptr sret(%enum.Token) %0, ptr %1) #2 {
entry:
  %c = alloca i8, align 1, !dbg !25
  %2 = call i8 @next_char(ptr %1), !dbg !28
  store i8 %2, ptr %c, align 1, !dbg !28
  %c1 = load i8, ptr %c, align 1, !dbg !29
  %cmp = icmp sle i8 %c1, 0, !dbg !30
  %tobool = icmp ne i1 %cmp, false, !dbg !30
  br i1 %tobool, label %if.stmt.then, label %if.stmt.end, !dbg !30

if.stmt.then:                                     ; preds = %entry
  %enum = alloca %enum.Token, align 8, !dbg !31
  %tag = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 0, !dbg !31
  %buf = getelementptr inbounds %enum.Token, ptr %enum, i32 0, i32 1, !dbg !31
  store i32 0, ptr %tag, align 4, !dbg !31
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %enum, i32 260, i1 false), !dbg !31
  br label %defers, !dbg !31

after.return:                                     ; No predecessors!
  br label %if.stmt.end, !dbg !33

if.stmt.end:                                      ; preds = %after.return, %entry
  %c2 = load i8, ptr %c, align 1, !dbg !34
  %widening = sext i8 %c2 to i32, !dbg !34
  %3 = call i32 @isspace(i32 %widening), !dbg !34
  %tobool3 = icmp ne i32 %3, 0, !dbg !34
  br i1 %tobool3, label %if.stmt.then4, label %if.stmt.end14, !dbg !34

if.stmt.then4:                                    ; preds = %if.stmt.end
  %4 = call i8 @next_char(ptr %1), !dbg !35
  store i8 %4, ptr %c, align 1, !dbg !35
  %c5 = load i8, ptr %c, align 1, !dbg !37
  %cmp6 = icmp sle i8 %c5, 0, !dbg !38
  %tobool7 = icmp ne i1 %cmp6, false, !dbg !38
  br i1 %tobool7, label %if.stmt.then8, label %if.stmt.end13, !dbg !38

if.stmt.then8:                                    ; preds = %if.stmt.then4
  %enum9 = alloca %enum.Token, align 8, !dbg !39
  %tag10 = getelementptr inbounds %enum.Token, ptr %enum9, i32 0, i32 0, !dbg !39
  %buf11 = getelementptr inbounds %enum.Token, ptr %enum9, i32 0, i32 1, !dbg !39
  store i32 0, ptr %tag10, align 4, !dbg !39
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %enum9, i32 260, i1 false), !dbg !39
  br label %defers, !dbg !39

after.return12:                                   ; No predecessors!
  br label %if.stmt.end13, !dbg !41

if.stmt.end13:                                    ; preds = %after.return12, %if.stmt.then4
  br label %if.stmt.end14, !dbg !41

if.stmt.end14:                                    ; preds = %if.stmt.end13, %if.stmt.end
  %c15 = load i8, ptr %c, align 1, !dbg !42
  %widening16 = sext i8 %c15 to i32, !dbg !42
  %5 = call i32 @isdigit(i32 %widening16), !dbg !42
  %tobool17 = icmp ne i32 %5, 0, !dbg !42
  br i1 %tobool17, label %if.stmt.then18, label %if.stmt.end21, !dbg !42

if.stmt.then18:                                   ; preds = %if.stmt.end14
  %c19 = load i8, ptr %c, align 1, !dbg !43
  %sret = alloca %enum.Token, align 8, !dbg !43
  call void @integer_literal(ptr sret(%enum.Token) %sret, ptr %1, i8 %c19), !dbg !43
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %sret, i32 260, i1 false), !dbg !43
  br label %defers, !dbg !43

after.return20:                                   ; No predecessors!
  br label %if.stmt.end21, !dbg !45

if.stmt.end21:                                    ; preds = %after.return20, %if.stmt.end14
  %c22 = load i8, ptr %c, align 1, !dbg !46
  %comp = icmp eq i8 %c22, 40, !dbg !47
  br i1 %comp, label %match.arm, label %6, !dbg !47

6:                                                ; preds = %if.stmt.end21
  %comp23 = icmp eq i8 %c22, 41, !dbg !48
  br i1 %comp23, label %match.arm53, label %7, !dbg !48

7:                                                ; preds = %6
  %comp24 = icmp eq i8 %c22, 123, !dbg !49
  br i1 %comp24, label %match.arm57, label %8, !dbg !49

8:                                                ; preds = %7
  %comp25 = icmp eq i8 %c22, 125, !dbg !50
  br i1 %comp25, label %match.arm61, label %9, !dbg !50

9:                                                ; preds = %8
  %comp26 = icmp eq i8 %c22, 91, !dbg !51
  br i1 %comp26, label %match.arm65, label %10, !dbg !51

10:                                               ; preds = %9
  %comp27 = icmp eq i8 %c22, 93, !dbg !52
  br i1 %comp27, label %match.arm69, label %11, !dbg !52

11:                                               ; preds = %10
  %comp28 = icmp eq i8 %c22, 59, !dbg !53
  br i1 %comp28, label %match.arm73, label %12, !dbg !53

12:                                               ; preds = %11
  %comp29 = icmp eq i8 %c22, 43, !dbg !54
  br i1 %comp29, label %match.arm77, label %13, !dbg !54

13:                                               ; preds = %12
  %comp30 = icmp eq i8 %c22, 45, !dbg !55
  br i1 %comp30, label %match.arm81, label %14, !dbg !55

14:                                               ; preds = %13
  %comp31 = icmp eq i8 %c22, 42, !dbg !56
  br i1 %comp31, label %match.arm85, label %15, !dbg !56

15:                                               ; preds = %14
  %comp32 = icmp eq i8 %c22, 47, !dbg !57
  br i1 %comp32, label %match.arm89, label %16, !dbg !57

16:                                               ; preds = %15
  %comp33 = icmp eq i8 %c22, 37, !dbg !58
  br i1 %comp33, label %match.arm93, label %17, !dbg !58

17:                                               ; preds = %16
  %comp34 = icmp eq i8 %c22, 38, !dbg !59
  br i1 %comp34, label %match.arm97, label %18, !dbg !59

18:                                               ; preds = %17
  %comp35 = icmp eq i8 %c22, 124, !dbg !60
  br i1 %comp35, label %match.arm101, label %19, !dbg !60

19:                                               ; preds = %18
  %comp36 = icmp eq i8 %c22, 94, !dbg !61
  br i1 %comp36, label %match.arm105, label %20, !dbg !61

20:                                               ; preds = %19
  %comp37 = icmp eq i8 %c22, 126, !dbg !62
  br i1 %comp37, label %match.arm109, label %21, !dbg !62

21:                                               ; preds = %20
  %comp38 = icmp eq i8 %c22, 33, !dbg !63
  br i1 %comp38, label %match.arm113, label %22, !dbg !63

22:                                               ; preds = %21
  %comp39 = icmp eq i8 %c22, 61, !dbg !64
  br i1 %comp39, label %match.arm117, label %23, !dbg !64

23:                                               ; preds = %22
  %comp40 = icmp eq i8 %c22, 60, !dbg !65
  br i1 %comp40, label %match.arm121, label %24, !dbg !65

24:                                               ; preds = %23
  %comp41 = icmp eq i8 %c22, 62, !dbg !66
  br i1 %comp41, label %match.arm125, label %25, !dbg !66

25:                                               ; preds = %24
  %comp42 = icmp eq i8 %c22, 63, !dbg !67
  br i1 %comp42, label %match.arm129, label %26, !dbg !67

26:                                               ; preds = %25
  %comp43 = icmp eq i8 %c22, 58, !dbg !68
  br i1 %comp43, label %match.arm133, label %27, !dbg !68

27:                                               ; preds = %26
  %comp44 = icmp eq i8 %c22, 46, !dbg !69
  br i1 %comp44, label %match.arm137, label %28, !dbg !69

28:                                               ; preds = %27
  %comp45 = icmp eq i8 %c22, 44, !dbg !70
  br i1 %comp45, label %match.arm141, label %29, !dbg !70

29:                                               ; preds = %28
  %comp46 = icmp eq i8 %c22, 34, !dbg !71
  br i1 %comp46, label %match.arm145, label %match.otherwise, !dbg !71

match.otherwise:                                  ; preds = %29
  %enum47 = alloca %enum.Token, align 8, !dbg !72
  %tag48 = getelementptr inbounds %enum.Token, ptr %enum47, i32 0, i32 0, !dbg !72
  %buf49 = getelementptr inbounds %enum.Token, ptr %enum47, i32 0, i32 1, !dbg !72
  store i32 1, ptr %tag48, align 4, !dbg !72
  br label %match.post, !dbg !72

match.arm:                                        ; preds = %if.stmt.end21
  %enum50 = alloca %enum.Token, align 8, !dbg !73
  %tag51 = getelementptr inbounds %enum.Token, ptr %enum50, i32 0, i32 0, !dbg !73
  %buf52 = getelementptr inbounds %enum.Token, ptr %enum50, i32 0, i32 1, !dbg !73
  store i32 2, ptr %tag51, align 4, !dbg !73
  br label %match.post, !dbg !73

match.arm53:                                      ; preds = %6
  %enum54 = alloca %enum.Token, align 8, !dbg !74
  %tag55 = getelementptr inbounds %enum.Token, ptr %enum54, i32 0, i32 0, !dbg !74
  %buf56 = getelementptr inbounds %enum.Token, ptr %enum54, i32 0, i32 1, !dbg !74
  store i32 3, ptr %tag55, align 4, !dbg !74
  br label %match.post, !dbg !74

match.arm57:                                      ; preds = %7
  %enum58 = alloca %enum.Token, align 8, !dbg !75
  %tag59 = getelementptr inbounds %enum.Token, ptr %enum58, i32 0, i32 0, !dbg !75
  %buf60 = getelementptr inbounds %enum.Token, ptr %enum58, i32 0, i32 1, !dbg !75
  store i32 4, ptr %tag59, align 4, !dbg !75
  br label %match.post, !dbg !75

match.arm61:                                      ; preds = %8
  %enum62 = alloca %enum.Token, align 8, !dbg !76
  %tag63 = getelementptr inbounds %enum.Token, ptr %enum62, i32 0, i32 0, !dbg !76
  %buf64 = getelementptr inbounds %enum.Token, ptr %enum62, i32 0, i32 1, !dbg !76
  store i32 5, ptr %tag63, align 4, !dbg !76
  br label %match.post, !dbg !76

match.arm65:                                      ; preds = %9
  %enum66 = alloca %enum.Token, align 8, !dbg !77
  %tag67 = getelementptr inbounds %enum.Token, ptr %enum66, i32 0, i32 0, !dbg !77
  %buf68 = getelementptr inbounds %enum.Token, ptr %enum66, i32 0, i32 1, !dbg !77
  store i32 6, ptr %tag67, align 4, !dbg !77
  br label %match.post, !dbg !77

match.arm69:                                      ; preds = %10
  %enum70 = alloca %enum.Token, align 8, !dbg !78
  %tag71 = getelementptr inbounds %enum.Token, ptr %enum70, i32 0, i32 0, !dbg !78
  %buf72 = getelementptr inbounds %enum.Token, ptr %enum70, i32 0, i32 1, !dbg !78
  store i32 7, ptr %tag71, align 4, !dbg !78
  br label %match.post, !dbg !78

match.arm73:                                      ; preds = %11
  %enum74 = alloca %enum.Token, align 8, !dbg !79
  %tag75 = getelementptr inbounds %enum.Token, ptr %enum74, i32 0, i32 0, !dbg !79
  %buf76 = getelementptr inbounds %enum.Token, ptr %enum74, i32 0, i32 1, !dbg !79
  store i32 8, ptr %tag75, align 4, !dbg !79
  br label %match.post, !dbg !79

match.arm77:                                      ; preds = %12
  %enum78 = alloca %enum.Token, align 8, !dbg !80
  %tag79 = getelementptr inbounds %enum.Token, ptr %enum78, i32 0, i32 0, !dbg !80
  %buf80 = getelementptr inbounds %enum.Token, ptr %enum78, i32 0, i32 1, !dbg !80
  store i32 9, ptr %tag79, align 4, !dbg !80
  br label %match.post, !dbg !80

match.arm81:                                      ; preds = %13
  %enum82 = alloca %enum.Token, align 8, !dbg !81
  %tag83 = getelementptr inbounds %enum.Token, ptr %enum82, i32 0, i32 0, !dbg !81
  %buf84 = getelementptr inbounds %enum.Token, ptr %enum82, i32 0, i32 1, !dbg !81
  store i32 10, ptr %tag83, align 4, !dbg !81
  br label %match.post, !dbg !81

match.arm85:                                      ; preds = %14
  %enum86 = alloca %enum.Token, align 8, !dbg !82
  %tag87 = getelementptr inbounds %enum.Token, ptr %enum86, i32 0, i32 0, !dbg !82
  %buf88 = getelementptr inbounds %enum.Token, ptr %enum86, i32 0, i32 1, !dbg !82
  store i32 11, ptr %tag87, align 4, !dbg !82
  br label %match.post, !dbg !82

match.arm89:                                      ; preds = %15
  %enum90 = alloca %enum.Token, align 8, !dbg !83
  %tag91 = getelementptr inbounds %enum.Token, ptr %enum90, i32 0, i32 0, !dbg !83
  %buf92 = getelementptr inbounds %enum.Token, ptr %enum90, i32 0, i32 1, !dbg !83
  store i32 12, ptr %tag91, align 4, !dbg !83
  br label %match.post, !dbg !83

match.arm93:                                      ; preds = %16
  %enum94 = alloca %enum.Token, align 8, !dbg !84
  %tag95 = getelementptr inbounds %enum.Token, ptr %enum94, i32 0, i32 0, !dbg !84
  %buf96 = getelementptr inbounds %enum.Token, ptr %enum94, i32 0, i32 1, !dbg !84
  store i32 13, ptr %tag95, align 4, !dbg !84
  br label %match.post, !dbg !84

match.arm97:                                      ; preds = %17
  %enum98 = alloca %enum.Token, align 8, !dbg !85
  %tag99 = getelementptr inbounds %enum.Token, ptr %enum98, i32 0, i32 0, !dbg !85
  %buf100 = getelementptr inbounds %enum.Token, ptr %enum98, i32 0, i32 1, !dbg !85
  store i32 14, ptr %tag99, align 4, !dbg !85
  br label %match.post, !dbg !85

match.arm101:                                     ; preds = %18
  %enum102 = alloca %enum.Token, align 8, !dbg !86
  %tag103 = getelementptr inbounds %enum.Token, ptr %enum102, i32 0, i32 0, !dbg !86
  %buf104 = getelementptr inbounds %enum.Token, ptr %enum102, i32 0, i32 1, !dbg !86
  store i32 15, ptr %tag103, align 4, !dbg !86
  br label %match.post, !dbg !86

match.arm105:                                     ; preds = %19
  %enum106 = alloca %enum.Token, align 8, !dbg !87
  %tag107 = getelementptr inbounds %enum.Token, ptr %enum106, i32 0, i32 0, !dbg !87
  %buf108 = getelementptr inbounds %enum.Token, ptr %enum106, i32 0, i32 1, !dbg !87
  store i32 16, ptr %tag107, align 4, !dbg !87
  br label %match.post, !dbg !87

match.arm109:                                     ; preds = %20
  %enum110 = alloca %enum.Token, align 8, !dbg !88
  %tag111 = getelementptr inbounds %enum.Token, ptr %enum110, i32 0, i32 0, !dbg !88
  %buf112 = getelementptr inbounds %enum.Token, ptr %enum110, i32 0, i32 1, !dbg !88
  store i32 17, ptr %tag111, align 4, !dbg !88
  br label %match.post, !dbg !88

match.arm113:                                     ; preds = %21
  %enum114 = alloca %enum.Token, align 8, !dbg !89
  %tag115 = getelementptr inbounds %enum.Token, ptr %enum114, i32 0, i32 0, !dbg !89
  %buf116 = getelementptr inbounds %enum.Token, ptr %enum114, i32 0, i32 1, !dbg !89
  store i32 18, ptr %tag115, align 4, !dbg !89
  br label %match.post, !dbg !89

match.arm117:                                     ; preds = %22
  %enum118 = alloca %enum.Token, align 8, !dbg !90
  %tag119 = getelementptr inbounds %enum.Token, ptr %enum118, i32 0, i32 0, !dbg !90
  %buf120 = getelementptr inbounds %enum.Token, ptr %enum118, i32 0, i32 1, !dbg !90
  store i32 19, ptr %tag119, align 4, !dbg !90
  br label %match.post, !dbg !90

match.arm121:                                     ; preds = %23
  %enum122 = alloca %enum.Token, align 8, !dbg !91
  %tag123 = getelementptr inbounds %enum.Token, ptr %enum122, i32 0, i32 0, !dbg !91
  %buf124 = getelementptr inbounds %enum.Token, ptr %enum122, i32 0, i32 1, !dbg !91
  store i32 20, ptr %tag123, align 4, !dbg !91
  br label %match.post, !dbg !91

match.arm125:                                     ; preds = %24
  %enum126 = alloca %enum.Token, align 8, !dbg !92
  %tag127 = getelementptr inbounds %enum.Token, ptr %enum126, i32 0, i32 0, !dbg !92
  %buf128 = getelementptr inbounds %enum.Token, ptr %enum126, i32 0, i32 1, !dbg !92
  store i32 21, ptr %tag127, align 4, !dbg !92
  br label %match.post, !dbg !92

match.arm129:                                     ; preds = %25
  %enum130 = alloca %enum.Token, align 8, !dbg !93
  %tag131 = getelementptr inbounds %enum.Token, ptr %enum130, i32 0, i32 0, !dbg !93
  %buf132 = getelementptr inbounds %enum.Token, ptr %enum130, i32 0, i32 1, !dbg !93
  store i32 22, ptr %tag131, align 4, !dbg !93
  br label %match.post, !dbg !93

match.arm133:                                     ; preds = %26
  %enum134 = alloca %enum.Token, align 8, !dbg !94
  %tag135 = getelementptr inbounds %enum.Token, ptr %enum134, i32 0, i32 0, !dbg !94
  %buf136 = getelementptr inbounds %enum.Token, ptr %enum134, i32 0, i32 1, !dbg !94
  store i32 23, ptr %tag135, align 4, !dbg !94
  br label %match.post, !dbg !94

match.arm137:                                     ; preds = %27
  %enum138 = alloca %enum.Token, align 8, !dbg !95
  %tag139 = getelementptr inbounds %enum.Token, ptr %enum138, i32 0, i32 0, !dbg !95
  %buf140 = getelementptr inbounds %enum.Token, ptr %enum138, i32 0, i32 1, !dbg !95
  store i32 24, ptr %tag139, align 4, !dbg !95
  br label %match.post, !dbg !95

match.arm141:                                     ; preds = %28
  %enum142 = alloca %enum.Token, align 8, !dbg !96
  %tag143 = getelementptr inbounds %enum.Token, ptr %enum142, i32 0, i32 0, !dbg !96
  %buf144 = getelementptr inbounds %enum.Token, ptr %enum142, i32 0, i32 1, !dbg !96
  store i32 25, ptr %tag143, align 4, !dbg !96
  br label %match.post, !dbg !96

match.arm145:                                     ; preds = %29
  %sret146 = alloca %enum.Token, align 8, !dbg !97
  call void @string_literal(ptr sret(%enum.Token) %sret146, ptr %1), !dbg !97
  br label %match.post, !dbg !97

match.post:                                       ; preds = %match.arm145, %match.arm141, %match.arm137, %match.arm133, %match.arm129, %match.arm125, %match.arm121, %match.arm117, %match.arm113, %match.arm109, %match.arm105, %match.arm101, %match.arm97, %match.arm93, %match.arm89, %match.arm85, %match.arm81, %match.arm77, %match.arm73, %match.arm69, %match.arm65, %match.arm61, %match.arm57, %match.arm53, %match.arm, %match.otherwise
  %match.result = phi ptr [ %enum47, %match.otherwise ], [ %enum50, %match.arm ], [ %enum54, %match.arm53 ], [ %enum58, %match.arm57 ], [ %enum62, %match.arm61 ], [ %enum66, %match.arm65 ], [ %enum70, %match.arm69 ], [ %enum74, %match.arm73 ], [ %enum78, %match.arm77 ], [ %enum82, %match.arm81 ], [ %enum86, %match.arm85 ], [ %enum90, %match.arm89 ], [ %enum94, %match.arm93 ], [ %enum98, %match.arm97 ], [ %enum102, %match.arm101 ], [ %enum106, %match.arm105 ], [ %enum110, %match.arm109 ], [ %enum114, %match.arm113 ], [ %enum118, %match.arm117 ], [ %enum122, %match.arm121 ], [ %enum126, %match.arm125 ], [ %enum130, %match.arm129 ], [ %enum134, %match.arm133 ], [ %enum138, %match.arm137 ], [ %enum142, %match.arm141 ], [ %sret146, %match.arm145 ], !dbg !46
  call void @llvm.memcpy.p0.p0.i32(ptr %0, ptr %match.result, i32 260, i1 false), !dbg !97
  br label %defers, !dbg !97

defers:                                           ; preds = %match.post, %if.stmt.then18, %if.stmt.then8, %if.stmt.then
  br label %return, !dbg !97

return:                                           ; preds = %defers
  ret void, !dbg !97
}

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare ptr @fopen(ptr, ptr) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @fclose(ptr) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @open(ptr, i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
declare i32 @close(i32) #0

; Function Attrs: nounwind sanitize_address memory(argmem: readwrite)
define i32 @haven_cimport_present() #0 {
entry:
  %retval = alloca i32, align 4, !dbg !98
  store i32 1, ptr %retval, align 4, !dbg !100
  br label %defers, !dbg !100

defers:                                           ; preds = %entry
  br label %return, !dbg !100

return:                                           ; preds = %defers
  %0 = load i32, ptr %retval, align 4, !dbg !100
  ret i32 %0, !dbg !100
}

; Function Attrs: nounwind sanitize_address memory(readwrite)
define i32 @haven_cimport_process(ptr %0) #2 {
entry:
  %retval = alloca i32, align 4, !dbg !102
  %fp = alloca ptr, align 8, !dbg !104
  %1 = call ptr @fopen(ptr %0, ptr @str.1), !dbg !106
  store ptr %1, ptr %fp, align 8, !dbg !106
  %fp1 = load ptr, ptr %fp, align 8, !dbg !107
  %2 = call i32 (ptr, ...) @printf(ptr @str.2, ptr %0, ptr %fp1), !dbg !107
  %tok = alloca %enum.Token, align 8, !dbg !108
  %fp2 = load ptr, ptr %fp, align 8, !dbg !109
  call void @next_token(ptr sret(%enum.Token) %tok, ptr %fp2), !dbg !109
  %fp3 = load ptr, ptr %fp, align 8, !dbg !110
  %3 = call i32 @fclose(ptr %fp3), !dbg !110
  store i32 -1, ptr %retval, align 4, !dbg !111
  br label %defers, !dbg !111

defers:                                           ; preds = %entry
  br label %return, !dbg !111

return:                                           ; preds = %defers
  %4 = load i32, ptr %retval, align 4, !dbg !111
  ret i32 %4, !dbg !111
}

attributes #0 = { nounwind sanitize_address memory(argmem: readwrite) }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nounwind sanitize_address memory(readwrite) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "haven", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false)
!1 = !DIFile(filename: "cimport.hv", directory: "/home/miselin/src/mattc/src/cimport")
!2 = !{i32 4, !"Debug Info Version", i32 3}
!3 = !{i32 4, !"Dwarf Version", i32 2}
!4 = !DILocation(line: 51, column: 10, scope: !5)
!5 = distinct !DILexicalBlock(scope: !6, file: !1, line: 50, column: 36)
!6 = distinct !DISubprogram(name: "string_literal", scope: null, file: !1, line: 50, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!7 = !DILocation(line: 55, column: 10, scope: !8)
!8 = distinct !DILexicalBlock(scope: !9, file: !1, line: 54, column: 52)
!9 = distinct !DISubprogram(name: "integer_literal", scope: null, file: !1, line: 54, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!10 = !DILocation(line: 59, column: 10, scope: !11)
!11 = distinct !DILexicalBlock(scope: !12, file: !1, line: 58, column: 47)
!12 = distinct !DISubprogram(name: "identifier", scope: null, file: !1, line: 58, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!13 = !DILocation(line: 62, column: 7, scope: !14)
!14 = distinct !DISubprogram(name: "next_char", scope: null, file: !1, line: 62, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!15 = !DILocation(line: 63, column: 25, scope: !16)
!16 = distinct !DILexicalBlock(scope: !14, file: !1, line: 62, column: 35)
!17 = !DILocation(line: 64, column: 8, scope: !16)
!18 = !DILocation(line: 64, column: 25, scope: !16)
!19 = !DILocation(line: 65, column: 10, scope: !16)
!20 = !DILocation(line: 66, column: 10, scope: !16)
!21 = !DILocation(line: 67, column: 10, scope: !16)
!22 = !DILocation(line: 68, column: 21, scope: !16)
!23 = !DILocation(line: 66, column: 16, scope: !16)
!24 = !DILocation(line: 67, column: 15, scope: !16)
!25 = !DILocation(line: 73, column: 8, scope: !26)
!26 = distinct !DILexicalBlock(scope: !27, file: !1, line: 72, column: 39)
!27 = distinct !DISubprogram(name: "next_token", scope: null, file: !1, line: 72, scopeLine: 1, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition, unit: !0)
!28 = !DILocation(line: 73, column: 29, scope: !26)
!29 = !DILocation(line: 74, column: 7, scope: !26)
!30 = !DILocation(line: 74, column: 12, scope: !26)
!31 = !DILocation(line: 75, column: 12, scope: !32)
!32 = distinct !DILexicalBlock(scope: !26, file: !1, line: 74, column: 16)
!33 = !DILocation(line: 1, column: 1, scope: !32)
!34 = !DILocation(line: 78, column: 24, scope: !26)
!35 = !DILocation(line: 79, column: 25, scope: !36)
!36 = distinct !DILexicalBlock(scope: !26, file: !1, line: 78, column: 27)
!37 = !DILocation(line: 80, column: 11, scope: !36)
!38 = !DILocation(line: 80, column: 16, scope: !36)
!39 = !DILocation(line: 81, column: 16, scope: !40)
!40 = distinct !DILexicalBlock(scope: !36, file: !1, line: 80, column: 20)
!41 = !DILocation(line: 1, column: 1, scope: !40)
!42 = !DILocation(line: 85, column: 24, scope: !26)
!43 = !DILocation(line: 86, column: 34, scope: !44)
!44 = distinct !DILexicalBlock(scope: !26, file: !1, line: 85, column: 27)
!45 = !DILocation(line: 1, column: 1, scope: !44)
!46 = !DILocation(line: 89, column: 10, scope: !26)
!47 = !DILocation(line: 90, column: 12, scope: !26)
!48 = !DILocation(line: 91, column: 12, scope: !26)
!49 = !DILocation(line: 92, column: 12, scope: !26)
!50 = !DILocation(line: 93, column: 12, scope: !26)
!51 = !DILocation(line: 94, column: 12, scope: !26)
!52 = !DILocation(line: 95, column: 12, scope: !26)
!53 = !DILocation(line: 96, column: 12, scope: !26)
!54 = !DILocation(line: 97, column: 12, scope: !26)
!55 = !DILocation(line: 98, column: 12, scope: !26)
!56 = !DILocation(line: 99, column: 12, scope: !26)
!57 = !DILocation(line: 100, column: 12, scope: !26)
!58 = !DILocation(line: 101, column: 12, scope: !26)
!59 = !DILocation(line: 102, column: 12, scope: !26)
!60 = !DILocation(line: 103, column: 12, scope: !26)
!61 = !DILocation(line: 104, column: 12, scope: !26)
!62 = !DILocation(line: 105, column: 12, scope: !26)
!63 = !DILocation(line: 106, column: 12, scope: !26)
!64 = !DILocation(line: 107, column: 12, scope: !26)
!65 = !DILocation(line: 108, column: 12, scope: !26)
!66 = !DILocation(line: 109, column: 12, scope: !26)
!67 = !DILocation(line: 110, column: 12, scope: !26)
!68 = !DILocation(line: 111, column: 12, scope: !26)
!69 = !DILocation(line: 112, column: 12, scope: !26)
!70 = !DILocation(line: 113, column: 12, scope: !26)
!71 = !DILocation(line: 114, column: 12, scope: !26)
!72 = !DILocation(line: 115, column: 13, scope: !26)
!73 = !DILocation(line: 90, column: 15, scope: !26)
!74 = !DILocation(line: 91, column: 15, scope: !26)
!75 = !DILocation(line: 92, column: 15, scope: !26)
!76 = !DILocation(line: 93, column: 15, scope: !26)
!77 = !DILocation(line: 94, column: 15, scope: !26)
!78 = !DILocation(line: 95, column: 15, scope: !26)
!79 = !DILocation(line: 96, column: 15, scope: !26)
!80 = !DILocation(line: 97, column: 15, scope: !26)
!81 = !DILocation(line: 98, column: 15, scope: !26)
!82 = !DILocation(line: 99, column: 15, scope: !26)
!83 = !DILocation(line: 100, column: 15, scope: !26)
!84 = !DILocation(line: 101, column: 15, scope: !26)
!85 = !DILocation(line: 102, column: 15, scope: !26)
!86 = !DILocation(line: 103, column: 15, scope: !26)
!87 = !DILocation(line: 104, column: 15, scope: !26)
!88 = !DILocation(line: 105, column: 15, scope: !26)
!89 = !DILocation(line: 106, column: 15, scope: !26)
!90 = !DILocation(line: 107, column: 15, scope: !26)
!91 = !DILocation(line: 108, column: 15, scope: !26)
!92 = !DILocation(line: 109, column: 15, scope: !26)
!93 = !DILocation(line: 110, column: 15, scope: !26)
!94 = !DILocation(line: 111, column: 15, scope: !26)
!95 = !DILocation(line: 112, column: 15, scope: !26)
!96 = !DILocation(line: 113, column: 15, scope: !26)
!97 = !DILocation(line: 114, column: 33, scope: !26)
!98 = !DILocation(line: 17, column: 4, scope: !99)
!99 = distinct !DISubprogram(name: "haven_cimport_present", scope: null, file: !1, line: 17, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!100 = !DILocation(line: 18, column: 6, scope: !101)
!101 = distinct !DILexicalBlock(scope: !99, file: !1, line: 17, column: 37)
!102 = !DILocation(line: 21, column: 4, scope: !103)
!103 = distinct !DISubprogram(name: "haven_cimport_process", scope: null, file: !1, line: 21, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!104 = !DILocation(line: 22, column: 8, scope: !105)
!105 = distinct !DILexicalBlock(scope: !103, file: !1, line: 21, column: 56)
!106 = !DILocation(line: 22, column: 33, scope: !105)
!107 = !DILocation(line: 23, column: 48, scope: !105)
!108 = !DILocation(line: 25, column: 8, scope: !105)
!109 = !DILocation(line: 25, column: 28, scope: !105)
!110 = !DILocation(line: 27, column: 14, scope: !105)
!111 = !DILocation(line: 29, column: 6, scope: !105)
