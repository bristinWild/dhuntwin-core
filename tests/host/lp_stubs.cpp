/* Stand-ins for the three logos-protocol token/host-service hooks the
   generated C ABI links against; the real ones arrive with
   logos-protocol inside the nix build. Only for the host smoke test. */
extern "C" {
int lp_token_save(const char*, const char*) { return 0; }
int lp_token_save_inbound(const char*, const char*) { return 0; }
int lp_grant_host_services(const char*) { return 0; }
}
