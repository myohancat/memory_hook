#ifndef __SIMPLE_CLI_H_
#define __SIMPLE_CLI_H_

#define DEFAULT_SERV_PORT    3333
#define MAX_SESSION_CNT     10
#define SC_CLI_WELCOME_MSG  "Welcome to the test server ...!"
#define SC_CLI_PROMPT       "test$ "

typedef int (*SCCli_Fn)(void* session, int argc, char** argv);

typedef struct SCCli_S
{
    const char* m_pszCli;
    const char* m_pszHelp;
    SCCli_Fn     m_fnCli;

    struct SCCli_S* m_pNext;
}SCCli_T;

void sc_install_cli(SCCli_T* pCli);

#define CLI(cli_name, cli_help)    \
    int cli_##cli_name(void* session, int argc, char** argv); \
    SCCli_T g_sCLI_##cli_name =  \
    {    \
        #cli_name, \
        cli_help, \
        cli_##cli_name, \
        0 \
    }; \
    int cli_##cli_name(void* session, int argc, char** argv)

#define INSTALL_CLI(cli_name) sc_install_cli(&(g_sCLI_##cli_name))

int sc_cli_init();

void sc_cli_out(void* pSession, const char* pszFmt, ...);
void sc_cli_out_all(const char* pszFmt, ...);

void sc_cli_deinit();

#endif /* __SIMPLE_TELNET_SERVER_H_ */
