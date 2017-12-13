static int parse_set(struct Buffer *tmp, struct Buffer *s, unsigned long data, struct Buffer *err)
{
  bool query, unset, inv, reset;
  int r = 0;
  int idx = -1;
  const char *p = NULL;
  char scratch[_POSIX_PATH_MAX];
  char *myvar = NULL;
  struct HashElem *he = NULL;
  struct ConfigDef *cdef = NULL;

  while (MoreArgs(s))
  {
    /* reset state variables */
    query = false;
    unset = (data & MUTT_SET_UNSET);
    inv = (data & MUTT_SET_INV);
    reset = (data & MUTT_SET_RESET);
    myvar = NULL;

    if (*s->dptr == '?')
    {
      query = true;
      s->dptr++;
    }
    else if (mutt_str_strncmp("no", s->dptr, 2) == 0)
    {
      s->dptr += 2;
      unset = !unset;
    }
    else if (mutt_str_strncmp("inv", s->dptr, 3) == 0)
    {
      s->dptr += 3;
      inv = !inv;
    }
    else if (*s->dptr == '&')
    {
      reset = true;
      s->dptr++;
    }

    /* get the variable name */
    mutt_extract_token(tmp, s, MUTT_TOKEN_EQUAL);

    if (mutt_str_strncmp("my_", tmp->data, 3) == 0)
      myvar = tmp->data;
    else
    {
      he = cs_get_elem(Config, tmp->data);
      cdef = he->data;
      idx = mutt_option_index(tmp->data);
      bool all = (mutt_str_strcmp("all", tmp->data) == 0);
      if (!he && (idx == -1) && !(reset && all))
      {
        snprintf(err->data, err->dsize, _("%s: unknown variable"), tmp->data);
        return -1;
      }
    }

    SKIPWS(s->dptr);

    if (reset)
    {
      if (query || unset || inv)
      {
        snprintf(err->data, err->dsize, "%s", _("prefix is illegal with reset"));
        return -1;
      }

      if (*s->dptr == '=')
      {
        snprintf(err->data, err->dsize, "%s", _("value is illegal with reset"));
        return -1;
      }

      if (mutt_str_strcmp("all", tmp->data) == 0)
      {
        if (CurrentMenu == MENU_PAGER)
        {
          snprintf(err->data, err->dsize, "%s", _("Not available in this menu."));
          return -1;
        }

        struct HashWalkState walk;
        memset(&walk, 0, sizeof(walk));

        struct HashElem *elem = NULL;
        while ((elem = mutt_hash_walk(Config->hash, &walk)))
          cs_he_reset(Config, elem, NULL);

        mutt_set_current_menu_redraw_full();
        OPT_SORT_SUBTHREADS = true;
        OPT_NEED_RESORT = true;
        OPT_RESORT_INIT = true;
        OPT_REDRAW_TREE = true;
        return 0;
      }
      else
      {
        CHECK_PAGER;
        if (myvar)
        {
          myvar_del(myvar);
        }
        else
        {
          //QWQ reset "debug_file" to debugfile_cmdline if set
          //QWQ reset "debug_level" to debuglevel_cmdline if set
          restore_default(cdef->flags);
          cs_he_reset(Config, he, NULL);
        }
      }
    }
    else if (!myvar && DTYPE(he->type) == DT_BOOL)
    {
      if (*s->dptr == '=')
      {
        if (unset || inv || query)
        {
          snprintf(err->data, err->dsize, "%s", _("Usage: set variable=yes|no"));
          return -1;
        }

        s->dptr++;
        mutt_extract_token(tmp, s, 0);
        if (mutt_str_strcasecmp("yes", tmp->data) == 0)
          unset = inv = 0;
        else if (mutt_str_strcasecmp("no", tmp->data) == 0)
          unset = 1;
        else
        {
          snprintf(err->data, err->dsize, "%s", _("Usage: set variable=yes|no"));
          return -1;
        }
      }

      if (query)
      {
        snprintf(err->data, err->dsize, *(bool *) cdef->var ? _("%s is set") : _("%s is unset"), tmp->data);
        return 0;
      }

      CHECK_PAGER;
      if (unset)
        *(bool *) cdef->var = false;
      else if (inv)
        *(bool *) cdef->var = !(*(bool *) cdef->var);
      else
        *(bool *) cdef->var = true;
    }
    else if (myvar || DTYPE(he->type) == DT_STRING || DTYPE(he->type) == DT_PATH || DTYPE(he->type) == DT_ADDRESS || DTYPE(he->type) == DT_MBTABLE)
    {
      if (unset)
      {
        CHECK_PAGER;
        if (myvar)
          myvar_del(myvar);
        else if (DTYPE(he->type) == DT_ADDRESS)
          mutt_addr_free((struct Address **) cdef->var);
        else if (DTYPE(he->type) == DT_MBTABLE)
          free_mbtable((struct MbTable **) cdef->var);
        else
          /* cdef->var is already 'char**' (or some 'void**') or...
           * so cast to 'void*' is okay */
          FREE((void *) cdef->var);
      }
      else if (query || *s->dptr != '=')
      {
        char tmp2[LONG_STRING];
        const char *val = NULL;

        if (myvar)
        {
          if ((val = myvar_get(myvar)))
          {
            pretty_var(err->data, err->dsize, myvar, val);
            break;
          }
          else
          {
            snprintf(err->data, err->dsize, _("%s: unknown variable"), myvar);
            return -1;
          }
        }
        else if (DTYPE(he->type) == DT_ADDRESS)
        {
          tmp2[0] = '\0';
          rfc822_write_address(tmp2, sizeof(tmp2), *((struct Address **) cdef->var), 0);
          val = tmp2;
        }
        else if (DTYPE(he->type) == DT_PATH)
        {
          tmp2[0] = '\0';
          mutt_str_strfcpy(tmp2, NONULL(*((char **) cdef->var)), sizeof(tmp2));
          mutt_pretty_mailbox(tmp2, sizeof(tmp2));
          val = tmp2;
        }
        else if (DTYPE(he->type) == DT_MBTABLE)
        {
          struct MbTable *mbt = (*((struct MbTable **) cdef->var));
          val = mbt ? NONULL(mbt->orig_str) : "";
        }
        else
          val = *((char **) cdef->var);

        /* user requested the value of this variable */
        pretty_var(err->data, err->dsize, cdef->name, NONULL(val));
        break;
      }
      else
      {
        CHECK_PAGER;
        s->dptr++;

        if (myvar)
        {
          /* myvar is a pointer to tmp and will be lost on extract_token */
          myvar = mutt_str_strdup(myvar);
          myvar_del(myvar);
        }

        mutt_extract_token(tmp, s, 0);

        if (myvar)
        {
          myvar_set(myvar, tmp->data);
          FREE(&myvar);
          myvar = "don't resort";
        }
        else if (DTYPE(he->type) == DT_PATH)
        {
          if (mutt_str_strcmp(cdef->name, "debug_file") == 0 && debugfile_cmdline)
          {
            mutt_message(_("set debug_file ignored, it has been overridden by "
                           "the cmdline"));
            break;
          }
          mutt_str_strfcpy(scratch, tmp->data, sizeof(scratch));
          mutt_expand_path(scratch, sizeof(scratch));
          cs_str_string_set(Config, cdef->name, scratch, NULL);
          if (mutt_str_strcmp(cdef->name, "debug_file") == 0)
            restart_debug();
        }
        else if (DTYPE(he->type) == DT_STRING)
        {
          if ((strstr(cdef->name, "charset") && check_charset(cdef, tmp->data) < 0) |
              /* $charset can't be empty, others can */
              ((strcmp(cdef->name, "charset") == 0) && !*tmp->data))
          {
            snprintf(err->data, err->dsize, _("Invalid value for option %s: \"%s\""), cdef->name, tmp->data);
            return -1;
          }

          cs_str_string_set(Config, cdef->name, tmp->data, NULL);
          if (mutt_str_strcmp(cdef->name, "charset") == 0)
            mutt_set_charset(Charset);

          if ((mutt_str_strcmp(cdef->name, "show_multipart_alternative") == 0) && !valid_show_multipart_alternative(tmp->data))
          {
            snprintf(err->data, err->dsize, _("Invalid value for name %s: \"%s\""), cdef->name, tmp->data);
            return -1;
          }
        }
        else if (DTYPE(he->type) == DT_MBTABLE)
        {
          free_mbtable((struct MbTable **) cdef->var);
          *((struct MbTable **) cdef->var) = parse_mbtable(tmp->data);
        }
        else
        {
          mutt_addr_free((struct Address **) cdef->var);
          *((struct Address **) cdef->var) = mutt_addr_parse_list(NULL, tmp->data);
        }
      }
    }
    else if (DTYPE(he->type) == DT_REGEX)
    {
      if (query || *s->dptr != '=')
      {
        /* user requested the value of this variable */
        struct Regex *ptr = *(struct Regex **) cdef->var;
        const char *value = ptr ? ptr->pattern : NULL;
        pretty_var(err->data, err->dsize, cdef->name, NONULL(value));
        break;
      }

      if (OPT_ATTACH_MSG && (mutt_str_strcmp(cdef->name, "reply_regexp") == 0))
      {
        snprintf(err->data, err->dsize, "Operation not permitted when in attach-message mode.");
        r = -1;
        break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token(tmp, s, 0);

      if (parse_regex(idx, tmp, err))
        /* $reply_regexp and $alternates require special treatment */
        if (Context && Context->msgcount && (mutt_str_strcmp(cdef->name, "reply_regexp") == 0))
        {
          regmatch_t pmatch[1];

          for (int i = 0; i < Context->msgcount; i++)
          {
            struct Envelope *e = Context->hdrs[i]->env;
            if (e && e->subject)
            {
              e->real_subj = (ReplyRegexp && (regexec(ReplyRegexp->regex, e->subject, 1, pmatch, 0))) ? e->subject : e->subject + pmatch[0].rm_eo;
            }
          }
        }
    }
    else if (DTYPE(he->type) == DT_MAGIC)
    {
      if (query || *s->dptr != '=')
      {
        switch (MboxType)
        {
          case MUTT_MBOX:
            p = "mbox";
            break;
          case MUTT_MMDF:
            p = "MMDF";
            break;
          case MUTT_MH:
            p = "MH";
            break;
          case MUTT_MAILDIR:
            p = "Maildir";
            break;
          default:
            p = "unknown";
            break;
        }
        snprintf(err->data, err->dsize, "%s=%s", cdef->name, p);
        break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token(tmp, s, 0);
      if (mx_set_magic(tmp->data))
      {
        snprintf(err->data, err->dsize, _("%s: invalid mailbox type"), tmp->data);
        r = -1;
        break;
      }
    }
    else if (DTYPE(he->type) == DT_NUMBER)
    {
      if (query || *s->dptr != '=')
      {
        short *ptr = (short *) cdef->var;

        short val = *ptr;
        /* compatibility alias */
        if (mutt_str_strcmp(cdef->name, "wrapmargin") == 0)
          val = *ptr < 0 ? -*ptr : 0;

        /* user requested the value of this variable */
        snprintf(err->data, err->dsize, "%s=%d", cdef->name, val);
        break;
      }

      CHECK_PAGER;
      s->dptr++;

      mutt_extract_token(tmp, s, 0);
      cs_he_string_set(Config, he, tmp->data, NULL);

      //QWQ validate: debug_level, history, imap_pipeline_depth, pager_index_lines, wrapmargin
    }
    else if (DTYPE(he->type) == DT_QUAD)
    {
      if (query)
      {
        cs_he_string_get(Config, he, err);
        break;
      }

      CHECK_PAGER;
      if (*s->dptr == '=')
      {
        s->dptr++;
        mutt_extract_token(tmp, s, 0);
        if (mutt_str_strcasecmp("yes", tmp->data) == 0)
          *(unsigned char *) cdef->var = MUTT_YES;
        else if (mutt_str_strcasecmp("no", tmp->data) == 0)
          *(unsigned char *) cdef->var = MUTT_NO;
        else if (mutt_str_strcasecmp("ask-yes", tmp->data) == 0)
          *(unsigned char *) cdef->var = MUTT_ASKYES;
        else if (mutt_str_strcasecmp("ask-no", tmp->data) == 0)
          *(unsigned char *) cdef->var = MUTT_ASKNO;
        else
        {
          snprintf(err->data, err->dsize, _("%s: invalid value"), tmp->data);
          r = -1;
          break;
        }
      }
      else
      {
        if (inv)
          *(unsigned char *) cdef->var = toggle_quadoption(*(unsigned char *) cdef->var);
        else if (unset)
          *(unsigned char *) cdef->var = MUTT_NO;
        else
          *(unsigned char *) cdef->var = MUTT_YES;
      }
    }
    else if (DTYPE(he->type) == DT_SORT)
    {
      const struct Mapping *map = NULL;

      switch (he->type & DT_SUBTYPE_MASK)
      {
        case DT_SORT_ALIAS:
          map = SortAliasMethods;
          break;
        case DT_SORT_BROWSER:
          map = SortBrowserMethods;
          break;
        case DT_SORT_KEYS:
          if ((WithCrypto & APPLICATION_PGP))
            map = SortKeyMethods;
          break;
        case DT_SORT_AUX:
          map = SortAuxMethods;
          break;
        case DT_SORT_SIDEBAR:
          map = SortSidebarMethods;
          break;
        default:
          map = SortMethods;
          break;
      }

      if (!map)
      {
        snprintf(err->data, err->dsize, _("%s: Unknown type."), cdef->name);
        r = -1;
        break;
      }

      if (query || *s->dptr != '=')
      {
        p = mutt_map_get_name(*((short *) cdef->var) & SORT_MASK, map);

        snprintf(err->data, err->dsize, "%s=%s%s%s", cdef->name, (*((short *) cdef->var) & SORT_REVERSE) ? "reverse-" : "", (*((short *) cdef->var) & SORT_LAST) ? "last-" : "", p);
        return 0;
      }
      CHECK_PAGER;
      s->dptr++;
      mutt_extract_token(tmp, s, 0);

      if (parse_sort((short *) cdef->var, tmp->data, map, err) == -1)
      {
        r = -1;
        break;
      }
    }
#ifdef USE_HCACHE
    else if (DTYPE(he->type) == DT_HCACHE)
    {
      if (query || (*s->dptr != '='))
      {
        pretty_var(err->data, err->dsize, cdef->name, NONULL((*(char **) cdef->var)));
        break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token(tmp, s, 0);
      if (mutt_hcache_is_valid_backend(tmp->data))
      {
        FREE((void *) cdef->var);
        *(char **) (cdef->var) = mutt_str_strdup(tmp->data);
      }
      else
      {
        snprintf(err->data, err->dsize, _("%s: invalid backend"), tmp->data);
        r = -1;
        break;
      }
    }
#endif
    else
    {
      snprintf(err->data, err->dsize, _("%s: unknown type"), cdef->name);
      r = -1;
      break;
    }

    if (!myvar)
    {
      if (cdef->flags & R_INDEX)
        mutt_set_menu_redraw_full(MENU_MAIN);
      if (cdef->flags & R_PAGER)
        mutt_set_menu_redraw_full(MENU_PAGER);
      if (cdef->flags & R_PAGER_FLOW)
      {
        mutt_set_menu_redraw_full(MENU_PAGER);
        mutt_set_menu_redraw(MENU_PAGER, REDRAW_FLOW);
      }
      if (cdef->flags & R_RESORT_SUB)
        OPT_SORT_SUBTHREADS = true;
      if (cdef->flags & R_RESORT)
        OPT_NEED_RESORT = true;
      if (cdef->flags & R_RESORT_INIT)
        OPT_RESORT_INIT = true;
      if (cdef->flags & R_TREE)
        OPT_REDRAW_TREE = true;
      if (cdef->flags & R_REFLOW)
        mutt_reflow_windows();
#ifdef USE_SIDEBAR
      if (cdef->flags & R_SIDEBAR)
        mutt_set_current_menu_redraw(REDRAW_SIDEBAR);
#endif
      if (cdef->flags & R_MENU)
        mutt_set_current_menu_redraw_full();
    }
  }
  return r;
}
