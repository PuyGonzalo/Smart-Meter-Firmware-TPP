typedef struct
{
  atparser_AnswerExpect_t answer_expected; /* expected answer type for this command */
  uint8_t endstr[AT_CMD_MAX_END_STR_SIZE]; /* termination string for AT cmd */

  /* save ptr on input buffer */
  at_buf_t *p_cmd_input;

} atparser_context_t;