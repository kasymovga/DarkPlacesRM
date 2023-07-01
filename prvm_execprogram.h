// NEED to reset startst after calling this! startst may or may not be clobbered!
#define ADVANCE_PROFILE_BEFORE_JUMP() \
	prog->xfunction->profile += (st - startst); \
	if (prvm_statementprofiling.integer || (prvm_coverage.integer & 4)) { \
		/* All statements from startst+1 to st have been hit. */ \
		while (++startst <= st) { \
			if (prog->statement_profile[startst - cached_statements]++ == 0 && (prvm_coverage.integer & 4)) \
				PRVM_StatementCoverageEvent(prog, prog->xfunction, startst - cached_statements); \
		} \
		/* Observe: startst now is clobbered (now at st+1)! */ \
	}

#ifdef PRVMTIMEPROFILING
#define PRE_ERROR() \
	ADVANCE_PROFILE_BEFORE_JUMP(); \
	prog->xstatement = st - cached_statements; \
	tm = Sys_DirtyTime(); \
	prog->xfunction->tprofile += (tm - starttm >= 0 && tm - starttm < 1800) ? (tm - starttm) : 0; \
	startst = st; \
	starttm = tm
#else
#define PRE_ERROR() \
	ADVANCE_PROFILE_BEFORE_JUMP(); \
	prog->xstatement = st - cached_statements; \
	startst = st
#endif

// This code isn't #ifdef/#define protectable, don't try.

#if HAVE_COMPUTED_GOTOS && !(PRVMSLOWINTERPRETER || PRVMTIMEPROFILING)
  // NOTE: Due to otherwise duplicate labels, only ONE interpreter path may
  // ever hit this!
# define USE_COMPUTED_GOTOS 1
#endif

#if USE_COMPUTED_GOTOS
  // Must exactly match opcode_e enum in pr_comp.h
    const static void *dispatchtable[] = {
	&&handle_OP_DONE, // 0
	&&handle_OP_MUL_F, // 1
	&&handle_OP_MUL_V, // 2
	&&handle_OP_MUL_FV, // 3
	&&handle_OP_MUL_VF, // 4
	&&handle_OP_DIV_F, // 5
	&&handle_OP_ADD_F, // 6
	&&handle_OP_ADD_V, // 7
	&&handle_OP_SUB_F, // 8
	&&handle_OP_SUB_V, // 9

	&&handle_OP_EQ_F, // 10
	&&handle_OP_EQ_V, // 11
	&&handle_OP_EQ_S, // 12
	&&handle_OP_EQ_E, // 13
	&&handle_OP_EQ_FNC, // 14

	&&handle_OP_NE_F, // 15
	&&handle_OP_NE_V, // 16
	&&handle_OP_NE_S, // 17
	&&handle_OP_NE_E, // 18
	&&handle_OP_NE_FNC, // 19

	&&handle_OP_LE, // 20
	&&handle_OP_GE, // 21
	&&handle_OP_LT, // 22
	&&handle_OP_GT, // 23

	&&handle_OP_LOAD_F, // 24
	&&handle_OP_LOAD_V, // 25
	&&handle_OP_LOAD_S, // 26
	&&handle_OP_LOAD_ENT, // 27
	&&handle_OP_LOAD_FLD, // 28
	&&handle_OP_LOAD_FNC, // 29

	&&handle_OP_ADDRESS, // 30

	&&handle_OP_STORE_F, // 31
	&&handle_OP_STORE_V, // 32
	&&handle_OP_STORE_S, // 33
	&&handle_OP_STORE_ENT, // 34
	&&handle_OP_STORE_FLD, // 35
	&&handle_OP_STORE_FNC, // 36

	&&handle_OP_STOREP_F, // 37
	&&handle_OP_STOREP_V, // 38
	&&handle_OP_STOREP_S, // 39
	&&handle_OP_STOREP_ENT, // 40
	&&handle_OP_STOREP_FLD, // 41
	&&handle_OP_STOREP_FNC, // 42

	&&handle_OP_RETURN, // 43
	&&handle_OP_NOT_F, // 44
	&&handle_OP_NOT_V, // 45
	&&handle_OP_NOT_S, // 46
	&&handle_OP_NOT_ENT, // 47
	&&handle_OP_NOT_FNC, // 48
	&&handle_OP_IF, // 49
	&&handle_OP_IFNOT, // 50
	&&handle_OP_CALL0, // 51
	&&handle_OP_CALL1, // 52
	&&handle_OP_CALL2, // 53
	&&handle_OP_CALL3, // 54
	&&handle_OP_CALL4, // 55
	&&handle_OP_CALL5, // 56
	&&handle_OP_CALL6, // 57
	&&handle_OP_CALL7, // 58
	&&handle_OP_CALL8, // 59
	&&handle_OP_STATE, // 60
	&&handle_OP_GOTO, // 61
	&&handle_OP_AND, // 62
	&&handle_OP_OR, // 63

	&&handle_OP_BITAND, // 64
	&&handle_OP_BITOR, // 65

    &&handle_OP_ERROR,    // 66
    &&handle_OP_ERROR,    // 67
    &&handle_OP_ERROR,    // 68
    &&handle_OP_ERROR,    // 69
    &&handle_OP_ERROR,    // 70
    &&handle_OP_ERROR,    // 71
    &&handle_OP_ERROR,    // 72
    &&handle_OP_ERROR,    // 73
    &&handle_OP_ERROR,    // 74
    &&handle_OP_ERROR,    // 75
    &&handle_OP_ERROR,    // 76
    &&handle_OP_ERROR,    // 77
    &&handle_OP_ERROR,    // 78
    &&handle_OP_ERROR,    // 79
    &&handle_OP_FETCH_GBL_F,      // 80
    &&handle_OP_FETCH_GBL_V,      // 81
    &&handle_OP_FETCH_GBL_S,      // 82
    &&handle_OP_FETCH_GBL_E,      // 83
    &&handle_OP_FETCH_GBL_FNC,    // 84
    &&handle_OP_ERROR,    // 85
    &&handle_OP_ERROR,    // 86
    &&handle_OP_ERROR,    // 87
    &&handle_OP_ERROR,    // 88
    &&handle_OP_ERROR,    // 89
    &&handle_OP_ERROR,    // 90
    &&handle_OP_ERROR,    // 91
    &&handle_OP_ERROR,    // 92
    &&handle_OP_ERROR,    // 93
    &&handle_OP_ERROR,    // 94
    &&handle_OP_ERROR,    // 95
    &&handle_OP_ERROR,    // 96
    &&handle_OP_ERROR,    // 97
    &&handle_OP_ERROR,    // 98
    &&handle_OP_ERROR,    // 99
    &&handle_OP_ERROR,    // 100
    &&handle_OP_ERROR,    // 101
    &&handle_OP_ERROR,    // 102
    &&handle_OP_ERROR,    // 103
    &&handle_OP_ERROR,    // 104
    &&handle_OP_ERROR,    // 105
    &&handle_OP_ERROR,    // 106
    &&handle_OP_ERROR,    // 107
    &&handle_OP_ERROR,    // 108
    &&handle_OP_ERROR,    // 109
    &&handle_OP_ERROR,    // 110
    &&handle_OP_ERROR,    // 111
    &&handle_OP_ERROR,    // 112
    &&handle_OP_ERROR,    // 113
    &&handle_OP_ERROR,    // 114
    &&handle_OP_ERROR,    // 115
    &&handle_OP_ERROR,    // 116
    &&handle_OP_ERROR,    // 117
    &&handle_OP_ERROR,    // 118
    &&handle_OP_ERROR,    // 119
    &&handle_OP_ERROR,    // 120
    &&handle_OP_ERROR,    // 121
    &&handle_OP_ERROR,    // 122
    &&handle_OP_CONV_FTOI,    // 123
    &&handle_OP_ERROR,    // 124
    &&handle_OP_ERROR,    // 125
    &&handle_OP_ERROR,    // 126
    &&handle_OP_ERROR,    // 127
    &&handle_OP_ERROR,    // 128
    &&handle_OP_ERROR,    // 129
    &&handle_OP_ERROR,    // 130
    &&handle_OP_ERROR,    // 131
    &&handle_OP_MUL_I,    // 132
    &&handle_OP_ERROR,    // 133
    &&handle_OP_ERROR,    // 134
    &&handle_OP_ERROR,    // 135
    &&handle_OP_ERROR,    // 136
    &&handle_OP_ERROR,    // 137
    &&handle_OP_ERROR,    // 138
    &&handle_OP_ERROR,    // 139
    &&handle_OP_ERROR,    // 140
    &&handle_OP_ERROR,    // 141
    &&handle_OP_ERROR,    // 142
    &&handle_OP_GLOBALADDRESS,    // 143
    &&handle_OP_ERROR,    // 144
    &&handle_OP_ERROR,    // 145
    &&handle_OP_ERROR,    // 146
    &&handle_OP_ERROR,    // 147
    &&handle_OP_ERROR,    // 148
    &&handle_OP_ERROR,    // 149
    &&handle_OP_ERROR,    // 150
    &&handle_OP_ERROR,    // 151
    &&handle_OP_ERROR,    // 152
    &&handle_OP_ERROR,    // 153
    &&handle_OP_ERROR,    // 154
    &&handle_OP_ERROR,    // 155
    &&handle_OP_ERROR,    // 156
    &&handle_OP_ERROR,    // 157
    &&handle_OP_ERROR,    // 158
    &&handle_OP_ERROR,    // 159
    &&handle_OP_ERROR,    // 160
    &&handle_OP_ERROR,    // 161
    &&handle_OP_ERROR,    // 162
    &&handle_OP_ERROR,    // 163
    &&handle_OP_ERROR,    // 164
    &&handle_OP_ERROR,    // 165
    &&handle_OP_ERROR,    // 166
    &&handle_OP_ERROR,    // 167
    &&handle_OP_ERROR,    // 168
    &&handle_OP_ERROR,    // 169
    &&handle_OP_ERROR,    // 170
    &&handle_OP_ERROR,    // 171
    &&handle_OP_ERROR,    // 172
    &&handle_OP_ERROR,    // 173
    &&handle_OP_ERROR,    // 174
    &&handle_OP_ERROR,    // 175
    &&handle_OP_ERROR,    // 176
    &&handle_OP_ERROR,    // 177
    &&handle_OP_ERROR,    // 178
    &&handle_OP_ERROR,    // 179
    &&handle_OP_ERROR,    // 180
    &&handle_OP_ERROR,    // 181
    &&handle_OP_ERROR,    // 182
    &&handle_OP_ERROR,    // 183
    &&handle_OP_ERROR,    // 184
    &&handle_OP_ERROR,    // 185
    &&handle_OP_ERROR,    // 186
    &&handle_OP_ERROR,    // 187
    &&handle_OP_ERROR,    // 188
    &&handle_OP_ERROR,    // 189
    &&handle_OP_ERROR,    // 190
    &&handle_OP_ERROR,    // 191
    &&handle_OP_ERROR,    // 192
    &&handle_OP_ERROR,    // 193
    &&handle_OP_ERROR,    // 194
    &&handle_OP_ERROR,    // 195
    &&handle_OP_ERROR,    // 196
    &&handle_OP_GSTOREP_I,    // 197
    &&handle_OP_GSTOREP_F,    // 198
    &&handle_OP_GSTOREP_ENT,  // 199
    &&handle_OP_GSTOREP_FLD,  // 200
    &&handle_OP_GSTOREP_S,    // 201
    &&handle_OP_GSTOREP_FNC,  // 202
    &&handle_OP_GSTOREP_V,    // 203
    &&handle_OP_ERROR,    // 204
    &&handle_OP_ERROR,    // 205
    &&handle_OP_ERROR,    // 206
    &&handle_OP_ERROR,    // 207
    &&handle_OP_ERROR,    // 208
    &&handle_OP_ERROR,    // 209
    &&handle_OP_ERROR,    // 210
    &&handle_OP_BOUNDCHECK,   // 211
    &&handle_OP_ERROR,    // 212
    &&handle_OP_ERROR,    // 213
    &&handle_OP_ERROR,    // 214
    &&handle_OP_ERROR,    // 215
    &&handle_OP_ERROR,    // 216
    &&handle_OP_ERROR,    // 217
    &&handle_OP_ERROR,    // 218
    &&handle_OP_ERROR,    // 219
    &&handle_OP_ERROR,    // 220
    &&handle_OP_ERROR,    // 221
    &&handle_OP_ERROR,    // 222
    &&handle_OP_ERROR,    // 223
    &&handle_OP_ERROR,    // 224
    &&handle_OP_ERROR,    // 225
    &&handle_OP_ERROR,    // 226
    &&handle_OP_ERROR,    // 227
    &&handle_OP_ERROR,    // 228
    &&handle_OP_ERROR,    // 229
    &&handle_OP_ERROR,    // 230
    &&handle_OP_ERROR,    // 231
    &&handle_OP_ERROR,    // 232
    &&handle_OP_ERROR,    // 233
    &&handle_OP_ERROR,    // 234
    &&handle_OP_ERROR,    // 235
    &&handle_OP_ERROR,    // 236
    &&handle_OP_ERROR,    // 237
    &&handle_OP_ERROR,    // 238
    &&handle_OP_ERROR,    // 239
    &&handle_OP_ERROR,    // 240
    &&handle_OP_ERROR,    // 241
    &&handle_OP_ERROR,    // 242
    &&handle_OP_ERROR,    // 243
    &&handle_OP_ERROR,    // 244
    &&handle_OP_ERROR,    // 245
    &&handle_OP_ERROR,    // 246
    &&handle_OP_ERROR,    // 247
    &&handle_OP_ERROR,    // 248
    &&handle_OP_ERROR,    // 249
    &&handle_OP_ERROR,    // 250
    &&handle_OP_ERROR,    // 251
    &&handle_OP_ERROR,    // 252
    &&handle_OP_ERROR,    // 253
    &&handle_OP_ERROR,    // 254
    &&handle_OP_ERROR,    // 255
	    };
#define DISPATCH_OPCODE() \
    goto *dispatchtable[(unsigned char)((++st)->op)]
#define HANDLE_OPCODE(opcode) handle_##opcode

    DISPATCH_OPCODE(); // jump to first opcode
#else // USE_COMPUTED_GOTOS
#define DISPATCH_OPCODE() break
#define HANDLE_OPCODE(opcode) case opcode

#if PRVMSLOWINTERPRETER
		{
			if (prog->watch_global_type != ev_void)
			{
				prvm_eval_t *g = PRVM_GLOBALFIELDVALUE(prog->watch_global);
				prog->xstatement = st + 1 - cached_statements;
				PRVM_Watchpoint(prog, 1, "Global watchpoint hit by engine", prog->watch_global_type, &prog->watch_global_value, g);
			}
			if (prog->watch_field_type != ev_void && prog->watch_edict < prog->max_edicts)
			{
				prvm_eval_t *g = PRVM_EDICTFIELDVALUE(prog->edicts + prog->watch_edict, prog->watch_field);
				prog->xstatement = st + 1 - cached_statements;
				PRVM_Watchpoint(prog, 1, "Entityfield watchpoint hit by engine", prog->watch_field_type, &prog->watch_edictfield_value, g);
			}
		}
#endif

		while (1)
		{
			st++;
#endif // USE_COMPUTED_GOTOS

#if !USE_COMPUTED_GOTOS

#if PRVMSLOWINTERPRETER
			if (prog->trace)
				PRVM_PrintStatement(prog, st);
			if (prog->break_statement >= 0)
				if ((st - cached_statements) == prog->break_statement)
				{
					prog->xstatement = st - cached_statements;
					PRVM_Breakpoint(prog, prog->break_stack_index, "Breakpoint hit");
				}
#endif
			switch (st->op)
			{
#endif
			HANDLE_OPCODE(OP_ADD_F):
				OPC->_float = OPA->_float + OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_ADD_V):
				OPC->vector[0] = OPA->vector[0] + OPB->vector[0];
				OPC->vector[1] = OPA->vector[1] + OPB->vector[1];
				OPC->vector[2] = OPA->vector[2] + OPB->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_SUB_F):
				OPC->_float = OPA->_float - OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_SUB_V):
				OPC->vector[0] = OPA->vector[0] - OPB->vector[0];
				OPC->vector[1] = OPA->vector[1] - OPB->vector[1];
				OPC->vector[2] = OPA->vector[2] - OPB->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_F):
				OPC->_float = OPA->_float * OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_V):
				OPC->_float = OPA->vector[0]*OPB->vector[0] + OPA->vector[1]*OPB->vector[1] + OPA->vector[2]*OPB->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_FV):
				tempfloat = OPA->_float;
				OPC->vector[0] = tempfloat * OPB->vector[0];
				OPC->vector[1] = tempfloat * OPB->vector[1];
				OPC->vector[2] = tempfloat * OPB->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_VF):
				tempfloat = OPB->_float;
				OPC->vector[0] = tempfloat * OPA->vector[0];
				OPC->vector[1] = tempfloat * OPA->vector[1];
				OPC->vector[2] = tempfloat * OPA->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_DIV_F):
				if( OPB->_float != 0.0f )
				{
					OPC->_float = OPA->_float / OPB->_float;
				}
				else
				{
					if (developer.integer > 0)
					{
                        PRE_ERROR();
						VM_Warning(prog, "%s: Attempted division by zero in: %s\n", prog->name, PRVM_GetString(prog, prog->xfunction->s_name));
					}
					OPC->_float = 0.0f;
				}
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITAND):
				OPC->_float = (prvm_int_t)OPA->_float & (prvm_int_t)OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITOR):
				OPC->_float = (prvm_int_t)OPA->_float | (prvm_int_t)OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GE):
				OPC->_float = OPA->_float >= OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LE):
				OPC->_float = OPA->_float <= OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GT):
				OPC->_float = OPA->_float > OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LT):
				OPC->_float = OPA->_float < OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_AND):
				OPC->_float = FLOAT_IS_TRUE_FOR_INT(OPA->_int) && FLOAT_IS_TRUE_FOR_INT(OPB->_int); // TODO change this back to float, and add AND_I to be used by fteqcc for anything not a float
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_OR):
				OPC->_float = FLOAT_IS_TRUE_FOR_INT(OPA->_int) || FLOAT_IS_TRUE_FOR_INT(OPB->_int); // TODO change this back to float, and add OR_I to be used by fteqcc for anything not a float
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_F):
				OPC->_float = !FLOAT_IS_TRUE_FOR_INT(OPA->_int);
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_V):
				OPC->_float = !OPA->vector[0] && !OPA->vector[1] && !OPA->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_S):
				OPC->_float = !OPA->string || !*PRVM_GetString(prog, OPA->string);
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_FNC):
				OPC->_float = !OPA->function;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_ENT):
				OPC->_float = (OPA->edict == 0);
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_F):
				OPC->_float = OPA->_float == OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_V):
				OPC->_float = (OPA->vector[0] == OPB->vector[0]) && (OPA->vector[1] == OPB->vector[1]) && (OPA->vector[2] == OPB->vector[2]);
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_S):
				OPC->_float = !strcmp(PRVM_GetString(prog, OPA->string),PRVM_GetString(prog, OPB->string));
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_E):
				OPC->_float = OPA->_int == OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_FNC):
				OPC->_float = OPA->function == OPB->function;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_F):
				OPC->_float = OPA->_float != OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_V):
				OPC->_float = (OPA->vector[0] != OPB->vector[0]) || (OPA->vector[1] != OPB->vector[1]) || (OPA->vector[2] != OPB->vector[2]);
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_S):
				OPC->_float = strcmp(PRVM_GetString(prog, OPA->string),PRVM_GetString(prog, OPB->string));
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_E):
				OPC->_float = OPA->_int != OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_FNC):
				OPC->_float = OPA->function != OPB->function;
				DISPATCH_OPCODE();

		//==================
			HANDLE_OPCODE(OP_STORE_F):
			HANDLE_OPCODE(OP_STORE_ENT):
			HANDLE_OPCODE(OP_STORE_FLD):		// integers
			HANDLE_OPCODE(OP_STORE_S):
			HANDLE_OPCODE(OP_STORE_FNC):		// pointers
				OPB->_int = OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_STORE_V):
				OPB->ivector[0] = OPA->ivector[0];
				OPB->ivector[1] = OPA->ivector[1];
				OPB->ivector[2] = OPA->ivector[2];
				DISPATCH_OPCODE();

            HANDLE_OPCODE(OP_STOREP_F):
            HANDLE_OPCODE(OP_STOREP_ENT):
            HANDLE_OPCODE(OP_STOREP_FLD):       // integers
            HANDLE_OPCODE(OP_STOREP_S):
            HANDLE_OPCODE(OP_STOREP_FNC):       // pointers
                if((prvm_uint_t)OPB->_int > cached_entityfieldsarea) {
                    prvm_int_t idx = OPB->_int - cached_entityfieldsarea;
                    prvm_int_t maxglobs = prog->numglobaldefs * 3;

                    if(idx >= maxglobs) {
                        PRE_ERROR();
                        Host_Error(prog, "%s attempted to write to an invalid indexed global %i (max %i) (op %i)", prog->name, idx, maxglobs, st->op);
                        goto cleanup;
                    }

                    ptr = (prvm_eval_t*)(prog->globals.ip + idx);
                    ptr->_int = OPA->_int;
                    DISPATCH_OPCODE();
                }

                if ((prvm_uint_t)OPB->_int - cached_entityfields >= cached_entityfieldsarea_entityfields)
				{
					if ((prvm_uint_t)OPB->_int >= cached_entityfieldsarea)
					{
						PRE_ERROR();
						Host_Error(prog, "%s attempted to write to an out of bounds edict (%i)", prog->name, (prvm_int_t)OPB->_int);
						goto cleanup;
					}
					if ((prvm_uint_t)OPB->_int < cached_entityfields && !cached_allowworldwrites)
					{
						PRE_ERROR();
						VM_Warning(prog, "assignment to world.%s (field %i) in %s\n", PRVM_GetString(prog, PRVM_ED_FieldAtOfs(prog, OPB->_int)->s_name), (prvm_int_t)OPB->_int, prog->name);
					}
				}
				ptr = (prvm_eval_t *)(cached_edictsfields + OPB->_int);
				ptr->_int = OPA->_int;
                DISPATCH_OPCODE();

            HANDLE_OPCODE(OP_STOREP_V):
                if((prvm_uint_t)OPB->_int > cached_entityfieldsarea) {
                    prvm_int_t idx = OPB->_int - cached_entityfieldsarea;
                    prvm_int_t maxglobs = prog->numglobaldefs * 3;

                    if(idx >= maxglobs) {
                        PRE_ERROR();
                        Host_Error(prog, "%s attempted to write to an invalid indexed global %i (max %i) (op %i)", prog->name, idx, maxglobs, st->op);
                        goto cleanup;
                    }

                    ptr = (prvm_eval_t*)(prog->globals.ip + idx);
                    ptr->ivector[0] = OPA->ivector[0];
                    ptr->ivector[1] = OPA->ivector[1];
                    ptr->ivector[2] = OPA->ivector[2];
                    DISPATCH_OPCODE();
                }

                if ((prvm_uint_t)OPB->_int - cached_entityfields > (prvm_uint_t)cached_entityfieldsarea_entityfields_3)
				{
					if ((prvm_uint_t)OPB->_int > cached_entityfieldsarea_3)
					{
						PRE_ERROR();
						Host_Error(prog, "%s attempted to write to an out of bounds edict (%i)", prog->name, (prvm_int_t)OPB->_int);
						goto cleanup;
					}
					if ((prvm_uint_t)OPB->_int < cached_entityfields && !cached_allowworldwrites)
					{
						PRE_ERROR();
						VM_Warning(prog, "assignment to world.%s (field %i) in %s\n", PRVM_GetString(prog, PRVM_ED_FieldAtOfs(prog, OPB->_int)->s_name), (prvm_int_t)OPB->_int, prog->name);
					}
				}
				ptr = (prvm_eval_t *)(cached_edictsfields + OPB->_int);
				ptr->ivector[0] = OPA->ivector[0];
				ptr->ivector[1] = OPA->ivector[1];
				ptr->ivector[2] = OPA->ivector[2];
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_ADDRESS):
				if ((prvm_uint_t)OPA->edict >= cached_max_edicts)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to address an out of bounds edict number", prog->name);
					goto cleanup;
				}
				if ((prvm_uint_t)OPB->_int >= cached_entityfields)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to address an invalid field (%i) in an edict", prog->name, (int)OPB->_int);
					goto cleanup;
				}
#if 0
				if (OPA->edict == 0 && !cached_allowworldwrites)
				{
					PRE_ERROR();
					Host_Error(prog, "forbidden assignment to null/world entity in %s", prog->name);
					goto cleanup;
				}
#endif
				OPC->_int = OPA->edict * cached_entityfields + OPB->_int;
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_LOAD_F):
			HANDLE_OPCODE(OP_LOAD_FLD):
			HANDLE_OPCODE(OP_LOAD_ENT):
			HANDLE_OPCODE(OP_LOAD_S):
			HANDLE_OPCODE(OP_LOAD_FNC):
				if ((prvm_uint_t)OPA->edict >= cached_max_edicts)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to read an out of bounds edict number", prog->name);
					goto cleanup;
				}
				if ((prvm_uint_t)OPB->_int >= cached_entityfields)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to read an invalid field in an edict (%i)", prog->name, (int)OPB->_int);
					goto cleanup;
				}
				ed = PRVM_PROG_TO_EDICT(OPA->edict);
				OPC->_int = ((prvm_eval_t *)(ed->fields.ip + OPB->_int))->_int;
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_LOAD_V):
				if ((prvm_uint_t)OPA->edict >= cached_max_edicts)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to read an out of bounds edict number", prog->name);
					goto cleanup;
				}
				if ((prvm_uint_t)OPB->_int > cached_entityfields_3)
				{
					PRE_ERROR();
					Host_Error(prog, "%s attempted to read an invalid field in an edict (%i)", prog->name, (int)OPB->_int);
					goto cleanup;
				}
				ed = PRVM_PROG_TO_EDICT(OPA->edict);
				ptr = (prvm_eval_t *)(ed->fields.ip + OPB->_int);
				OPC->ivector[0] = ptr->ivector[0];
				OPC->ivector[1] = ptr->ivector[1];
				OPC->ivector[2] = ptr->ivector[2];
				DISPATCH_OPCODE();

		//==================

			HANDLE_OPCODE(OP_IFNOT):
				if(!FLOAT_IS_TRUE_FOR_INT(OPA->_int))
				// TODO add an "int-if", and change this one to OPA->_float
				// although mostly unneeded, thanks to the only float being false being 0x0 and 0x80000000 (negative zero)
				// and entity, string, field values can never have that value
				{
					ADVANCE_PROFILE_BEFORE_JUMP();
					st = cached_statements + st->jumpabsolute - 1;	// offset the st++
					startst = st;
					// no bounds check needed, it is done when loading progs
					if (++jumpcount == 10000000 && prvm_runawaycheck)
					{
						prog->xstatement = st - cached_statements;
						PRVM_Profile(prog, 1<<30, 1000000, 0);
						Host_Error(prog, "%s runaway loop counter hit limit of %d jumps\ntip: read above for list of most-executed functions", prog->name, jumpcount);
					}
				}
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_IF):
				if(FLOAT_IS_TRUE_FOR_INT(OPA->_int))
				// TODO add an "int-if", and change this one, as well as the FLOAT_IS_TRUE_FOR_INT usages, to OPA->_float
				// although mostly unneeded, thanks to the only float being false being 0x0 and 0x80000000 (negative zero)
				// and entity, string, field values can never have that value
				{
					ADVANCE_PROFILE_BEFORE_JUMP();
					st = cached_statements + st->jumpabsolute - 1;	// offset the st++
					startst = st;
					// no bounds check needed, it is done when loading progs
					if (++jumpcount == 10000000 && prvm_runawaycheck)
					{
						prog->xstatement = st - cached_statements;
						PRVM_Profile(prog, 1<<30, 0.01, 0);
						Host_Error(prog, "%s runaway loop counter hit limit of %d jumps\ntip: read above for list of most-executed functions", prog->name, jumpcount);
					}
				}
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_GOTO):
				ADVANCE_PROFILE_BEFORE_JUMP();
				st = cached_statements + st->jumpabsolute - 1;	// offset the st++
				startst = st;
				// no bounds check needed, it is done when loading progs
				if (++jumpcount == 10000000 && prvm_runawaycheck)
				{
					prog->xstatement = st - cached_statements;
					PRVM_Profile(prog, 1<<30, 0.01, 0);
					Host_Error(prog, "%s runaway loop counter hit limit of %d jumps\ntip: read above for list of most-executed functions", prog->name, jumpcount);
				}
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_CALL0):
			HANDLE_OPCODE(OP_CALL1):
			HANDLE_OPCODE(OP_CALL2):
			HANDLE_OPCODE(OP_CALL3):
			HANDLE_OPCODE(OP_CALL4):
			HANDLE_OPCODE(OP_CALL5):
			HANDLE_OPCODE(OP_CALL6):
			HANDLE_OPCODE(OP_CALL7):
			HANDLE_OPCODE(OP_CALL8):
#ifdef PRVMTIMEPROFILING 
				tm = Sys_DirtyTime();
				prog->xfunction->tprofile += (tm - starttm >= 0 && tm - starttm < 1800) ? (tm - starttm) : 0;
				starttm = tm;
#endif
				ADVANCE_PROFILE_BEFORE_JUMP();
				startst = st;
				prog->xstatement = st - cached_statements;
				prog->argc = st->op - OP_CALL0;
				if (!OPA->function)
				{
					Host_Error(prog, "NULL function in %s", prog->name);
				}

				if(!OPA->function || OPA->function < 0 || OPA->function >= prog->numfunctions)
				{
					PRE_ERROR();
					Host_Error(prog, "%s CALL outside the program", prog->name);
					goto cleanup;
				}

				enterfunc = &prog->functions[OPA->function];
				if (enterfunc->callcount++ == 0 && (prvm_coverage.integer & 1))
					PRVM_FunctionCoverageEvent(prog, enterfunc);

				if (enterfunc->first_statement < 0)
				{
					// negative first_statement values are built in functions
					int builtinnumber = -enterfunc->first_statement;
					prog->xfunction->builtinsprofile++;
					if (builtinnumber < prog->numbuiltins && prog->builtins[builtinnumber])
					{
						prog->builtins[builtinnumber](prog);
#ifdef PRVMTIMEPROFILING 
						tm = Sys_DirtyTime();
						enterfunc->tprofile += (tm - starttm >= 0 && tm - starttm < 1800) ? (tm - starttm) : 0;
						prog->xfunction->tbprofile += (tm - starttm >= 0 && tm - starttm < 1800) ? (tm - starttm) : 0;
						starttm = tm;
#endif
						// builtins may cause ED_Alloc() to be called, update cached variables
						cached_edictsfields = prog->edictsfields;
						cached_entityfields = prog->entityfields;
						cached_entityfields_3 = prog->entityfields - 3;
						cached_entityfieldsarea = prog->entityfieldsarea;
						cached_entityfieldsarea_entityfields = prog->entityfieldsarea - prog->entityfields;
						cached_entityfieldsarea_3 = prog->entityfieldsarea - 3;
						cached_entityfieldsarea_entityfields_3 = prog->entityfieldsarea - prog->entityfields - 3;
						cached_max_edicts = prog->max_edicts;
						// these do not change
						//cached_statements = prog->statements;
						//cached_allowworldwrites = prog->allowworldwrites;
						//cached_flag = prog->flag;
						// if prog->trace changed we need to change interpreter path
						if (prog->trace != cachedpr_trace)
							goto chooseexecprogram;
					}
					else
						Host_Error(prog, "No such builtin #%i in %s; most likely cause: outdated engine build. Try updating!", builtinnumber, prog->name);
				}
				else
					st = cached_statements + PRVM_EnterFunction(prog, enterfunc);
				startst = st;
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_DONE):
			HANDLE_OPCODE(OP_RETURN):
#ifdef PRVMTIMEPROFILING 
				tm = Sys_DirtyTime();
				prog->xfunction->tprofile += (tm - starttm >= 0 && tm - starttm < 1800) ? (tm - starttm) : 0;
				starttm = tm;
#endif
				ADVANCE_PROFILE_BEFORE_JUMP();
				prog->xstatement = st - cached_statements;

				prog->globals.ip[OFS_RETURN  ] = prog->globals.ip[st->operand[0]  ];
				prog->globals.ip[OFS_RETURN+1] = prog->globals.ip[st->operand[0]+1];
				prog->globals.ip[OFS_RETURN+2] = prog->globals.ip[st->operand[0]+2];

				st = cached_statements + PRVM_LeaveFunction(prog);
				startst = st;
				if (prog->depth <= exitdepth)
					goto cleanup; // all done
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_STATE):
				if(cached_flag & PRVM_OP_STATE)
				{
					ed = PRVM_PROG_TO_EDICT(PRVM_gameglobaledict(self));
					PRVM_gameedictfloat(ed,nextthink) = PRVM_gameglobalfloat(time) + 0.1;
					PRVM_gameedictfloat(ed,frame) = OPA->_float;
					PRVM_gameedictfunction(ed,think) = OPB->function;
				}
				else
				{
					PRE_ERROR();
					prog->xstatement = st - cached_statements;
					Host_Error(prog, "OP_STATE not supported by %s", prog->name);
				}
				DISPATCH_OPCODE();

// LordHavoc: to be enabled when Progs version 7 (or whatever it will be numbered) is finalized
/*
			HANDLE_OPCODE(OP_ADD_I):
				OPC->_int = OPA->_int + OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_ADD_IF):
				OPC->_int = OPA->_int + (prvm_int_t) OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_ADD_FI):
				OPC->_float = OPA->_float + (prvm_vec_t) OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_SUB_I):
				OPC->_int = OPA->_int - OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_SUB_IF):
				OPC->_int = OPA->_int - (prvm_int_t) OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_SUB_FI):
				OPC->_float = OPA->_float - (prvm_vec_t) OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_I):
				OPC->_int = OPA->_int * OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_IF):
				OPC->_int = OPA->_int * (prvm_int_t) OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_FI):
				OPC->_float = OPA->_float * (prvm_vec_t) OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_MUL_VI):
				OPC->vector[0] = (prvm_vec_t) OPB->_int * OPA->vector[0];
				OPC->vector[1] = (prvm_vec_t) OPB->_int * OPA->vector[1];
				OPC->vector[2] = (prvm_vec_t) OPB->_int * OPA->vector[2];
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_DIV_VF):
				{
					float temp = 1.0f / OPB->_float;
					OPC->vector[0] = temp * OPA->vector[0];
					OPC->vector[1] = temp * OPA->vector[1];
					OPC->vector[2] = temp * OPA->vector[2];
				}
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_DIV_I):
				OPC->_int = OPA->_int / OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_DIV_IF):
				OPC->_int = OPA->_int / (prvm_int_t) OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_DIV_FI):
				OPC->_float = OPA->_float / (prvm_vec_t) OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_CONV_IF):
				OPC->_float = OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_CONV_FI):
				OPC->_int = OPA->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITAND_I):
				OPC->_int = OPA->_int & OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITOR_I):
				OPC->_int = OPA->_int | OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITAND_IF):
				OPC->_int = OPA->_int & (prvm_int_t)OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITOR_IF):
				OPC->_int = OPA->_int | (prvm_int_t)OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITAND_FI):
				OPC->_float = (prvm_int_t)OPA->_float & OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_BITOR_FI):
				OPC->_float = (prvm_int_t)OPA->_float | OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GE_I):
				OPC->_float = OPA->_int >= OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LE_I):
				OPC->_float = OPA->_int <= OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GT_I):
				OPC->_float = OPA->_int > OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LT_I):
				OPC->_float = OPA->_int < OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_AND_I):
				OPC->_float = OPA->_int && OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_OR_I):
				OPC->_float = OPA->_int || OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GE_IF):
				OPC->_float = (prvm_vec_t)OPA->_int >= OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LE_IF):
				OPC->_float = (prvm_vec_t)OPA->_int <= OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GT_IF):
				OPC->_float = (prvm_vec_t)OPA->_int > OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LT_IF):
				OPC->_float = (prvm_vec_t)OPA->_int < OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_AND_IF):
				OPC->_float = (prvm_vec_t)OPA->_int && OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_OR_IF):
				OPC->_float = (prvm_vec_t)OPA->_int || OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GE_FI):
				OPC->_float = OPA->_float >= (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LE_FI):
				OPC->_float = OPA->_float <= (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GT_FI):
				OPC->_float = OPA->_float > (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LT_FI):
				OPC->_float = OPA->_float < (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_AND_FI):
				OPC->_float = OPA->_float && (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_OR_FI):
				OPC->_float = OPA->_float || (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NOT_I):
				OPC->_float = !OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_I):
				OPC->_float = OPA->_int == OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_IF):
				OPC->_float = (prvm_vec_t)OPA->_int == OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_EQ_FI):
				OPC->_float = OPA->_float == (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_I):
				OPC->_float = OPA->_int != OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_IF):
				OPC->_float = (prvm_vec_t)OPA->_int != OPB->_float;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_NE_FI):
				OPC->_float = OPA->_float != (prvm_vec_t)OPB->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_STORE_I):
				OPB->_int = OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_STOREP_I):
#if PRBOUNDSCHECK
				if (OPB->_int < 0 || OPB->_int + 4 > pr_edictareasize)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to write to an out of bounds edict", prog->name);
					goto cleanup;
				}
#endif
				ptr = (prvm_eval_t *)(prog->edictsfields + OPB->_int);
				ptr->_int = OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_LOAD_I):
#if PRBOUNDSCHECK
				if (OPA->edict < 0 || OPA->edict >= prog->max_edicts)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to read an out of bounds edict number", prog->name);
					goto cleanup;
				}
				if (OPB->_int < 0 || OPB->_int >= progs->entityfields)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to read an invalid field in an edict", prog->name);
					goto cleanup;
				}
#endif
				ed = PRVM_PROG_TO_EDICT(OPA->edict);
				OPC->_int = ((prvm_eval_t *)((int *)ed->v + OPB->_int))->_int;
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_GSTOREP_I):
			HANDLE_OPCODE(OP_GSTOREP_F):
			HANDLE_OPCODE(OP_GSTOREP_ENT):
			HANDLE_OPCODE(OP_GSTOREP_FLD):		// integers
			HANDLE_OPCODE(OP_GSTOREP_S):
			HANDLE_OPCODE(OP_GSTOREP_FNC):		// pointers
#if PRBOUNDSCHECK
				if (OPB->_int < 0 || OPB->_int >= pr_globaldefs)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to write to an invalid indexed global", prog->name);
					goto cleanup;
				}
#endif
				pr_iglobals[OPB->_int] = OPA->_int;
				DISPATCH_OPCODE();
			HANDLE_OPCODE(OP_GSTOREP_V):
#if PRBOUNDSCHECK
				if (OPB->_int < 0 || OPB->_int + 2 >= pr_globaldefs)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to write to an invalid indexed global", prog->name);
					goto cleanup;
				}
#endif
				pr_iglobals[OPB->_int  ] = OPA->ivector[0];
				pr_iglobals[OPB->_int+1] = OPA->ivector[1];
				pr_iglobals[OPB->_int+2] = OPA->ivector[2];
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_GADDRESS):
				i = OPA->_int + (prvm_int_t) OPB->_float;
#if PRBOUNDSCHECK
				if (i < 0 || i >= pr_globaldefs)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to address an out of bounds global", prog->name);
					goto cleanup;
				}
#endif
				OPC->_int = pr_iglobals[i];
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_GLOAD_I):
			HANDLE_OPCODE(OP_GLOAD_F):
			HANDLE_OPCODE(OP_GLOAD_FLD):
			HANDLE_OPCODE(OP_GLOAD_ENT):
			HANDLE_OPCODE(OP_GLOAD_S):
			HANDLE_OPCODE(OP_GLOAD_FNC):
#if PRBOUNDSCHECK
				if (OPA->_int < 0 || OPA->_int >= pr_globaldefs)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to read an invalid indexed global", prog->name);
					goto cleanup;
				}
#endif
				OPC->_int = pr_iglobals[OPA->_int];
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_GLOAD_V):
#if PRBOUNDSCHECK
				if (OPA->_int < 0 || OPA->_int + 2 >= pr_globaldefs)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs attempted to read an invalid indexed global", prog->name);
					goto cleanup;
				}
#endif
				OPC->ivector[0] = pr_iglobals[OPA->_int  ];
				OPC->ivector[1] = pr_iglobals[OPA->_int+1];
				OPC->ivector[2] = pr_iglobals[OPA->_int+2];
				DISPATCH_OPCODE();

			HANDLE_OPCODE(OP_BOUNDCHECK):
				if (OPA->_int < 0 || OPA->_int >= st->b)
				{
					PRE_ERROR();
					Host_Error(prog, "%s Progs boundcheck failed at line number %d, value is < 0 or >= %d", prog->name, st->b, st->c);
					goto cleanup;
				}
				DISPATCH_OPCODE();

*/

            HANDLE_OPCODE(OP_GSTOREP_I):
            HANDLE_OPCODE(OP_GSTOREP_F):
            HANDLE_OPCODE(OP_GSTOREP_ENT):
            HANDLE_OPCODE(OP_GSTOREP_FLD):
            HANDLE_OPCODE(OP_GSTOREP_S):
            HANDLE_OPCODE(OP_GSTOREP_FNC): {
                prvm_int_t idx = OPB->_int - prog->entityfieldsarea;
                prvm_int_t maxidx = prog->numglobaldefs * 3;

                if(idx < 0 || idx >= maxidx) {
                    PRE_ERROR();
                    Host_Error(prog, "%s attempted to write to an invalid indexed global %i (max %i) (op %i)", prog->name, idx, maxidx, st->op);
                    goto cleanup;
                }

                ptr = (prvm_eval_t*)(prog->globals.ip + idx);
                ptr->_int = OPA->_int;
                DISPATCH_OPCODE();
            }

            HANDLE_OPCODE(OP_GSTOREP_V): {
                prvm_int_t idx = OPB->_int - prog->entityfieldsarea;
                prvm_int_t maxidx = prog->numglobaldefs * 3;

                if(idx < 0 || idx + 2 >= maxidx) {
                    PRE_ERROR();
                    Host_Error(prog, "%s attempted to write to an invalid indexed global %i (max %i) (op %i)", prog->name, idx, maxidx, st->op);
                    goto cleanup;
                }

                ptr = (prvm_eval_t*)(prog->globals.ip + idx);
                ptr->ivector[0] = OPA->ivector[0];
                ptr->ivector[1] = OPA->ivector[1];
                ptr->ivector[2] = OPA->ivector[2];
                DISPATCH_OPCODE();
            }

            HANDLE_OPCODE(OP_FETCH_GBL_F):
            HANDLE_OPCODE(OP_FETCH_GBL_S):
            HANDLE_OPCODE(OP_FETCH_GBL_E):
            HANDLE_OPCODE(OP_FETCH_GBL_FNC): {
                prvm_int_t idx = (prvm_int_t)OPB->_float;
                prvm_int_t maxidx = prog->globals.ip[st->operand[0] - 1];

                if(idx < 0 || idx > maxidx) {
                    PRE_ERROR();
                    Host_Error(prog, "%s array index out of bounds (index %i, max %i)", prog->name, idx, maxidx);
                    goto cleanup;
                }

                OPC->_int = prog->globals.ip[st->operand[0] + idx];
                DISPATCH_OPCODE();
            }

            HANDLE_OPCODE(OP_FETCH_GBL_V): {
                prvm_int_t idx = (prvm_int_t)OPB->_float;
                prvm_int_t maxidx = prog->globals.ip[st->operand[0] - 1];

                if(idx < 0 || idx > maxidx) {
                    PRE_ERROR();
                    Host_Error(prog, "%s array index out of bounds (index %i, max %i)", prog->name, idx, maxidx);
                    goto cleanup;
                }

                ptr = (prvm_eval_t*)(prog->globals.ip + st->operand[0] + 3 * idx);
                OPC->ivector[0] = ptr->ivector[0];
                OPC->ivector[1] = ptr->ivector[1];
                OPC->ivector[2] = ptr->ivector[2];
                DISPATCH_OPCODE();
            }

            HANDLE_OPCODE(OP_CONV_FTOI):
                OPC->_int = (prvm_int_t)OPA->_float;
                DISPATCH_OPCODE();

            HANDLE_OPCODE(OP_MUL_I):
                OPC->_int = OPA->_int * OPB->_int;
                DISPATCH_OPCODE();

            HANDLE_OPCODE(OP_GLOBALADDRESS):
                OPC->_int = st->operand[0] + OPB->_int + cached_entityfieldsarea;
                DISPATCH_OPCODE();

            HANDLE_OPCODE(OP_BOUNDCHECK):
                if((prvm_uint_t)OPA->_int < (prvm_uint_t)st->operand[2] || (prvm_uint_t)OPA->_int >= (prvm_uint_t)st->operand[1]) {
                    PRE_ERROR();
                    Host_Error(prog, "%s boundcheck failed. Value is %i. Must be between %u and %u", prog->name, OPA->_int, st->operand[2], st->operand[1]);
                    goto cleanup;
                }
                DISPATCH_OPCODE();

#if !USE_COMPUTED_GOTOS
			default:
				PRE_ERROR();
				Host_Error(prog, "Bad opcode %i in %s", st->op, prog->name);
				goto cleanup;
			}
#if PRVMSLOWINTERPRETER
			{
				if (prog->watch_global_type != ev_void)
				{
					prvm_eval_t *g = PRVM_GLOBALFIELDVALUE(prog->watch_global);
					prog->xstatement = st - cached_statements;
					PRVM_Watchpoint(prog, 0, "Global watchpoint hit", prog->watch_global_type, &prog->watch_global_value, g);
				}
				if (prog->watch_field_type != ev_void && prog->watch_edict < prog->max_edicts)
				{
					prvm_eval_t *g = PRVM_EDICTFIELDVALUE(prog->edicts + prog->watch_edict, prog->watch_field);
					prog->xstatement = st - cached_statements;
					PRVM_Watchpoint(prog, 0, "Entityfield watchpoint hit", prog->watch_field_type, &prog->watch_edictfield_value, g);
				}
			}
#endif
		}
#else
			HANDLE_OPCODE(OP_ERROR):
				PRE_ERROR();
				Host_Error(prog, "Bad opcode %i in %s", st->op, prog->name);
				goto cleanup;
#endif // !USE_COMPUTED_GOTOS

#undef DISPATCH_OPCODE
#undef HANDLE_OPCODE
#undef USE_COMPUTED_GOTOS
#undef PRE_ERROR
#undef ADVANCE_PROFILE_BEFORE_JUMP
