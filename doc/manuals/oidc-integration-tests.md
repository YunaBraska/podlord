# Test native OIDC against ZITADEL

The ordinary `native.authentication.oidc_*` tests use a local protocol boundary. They cover malformed discovery, device responses, token responses, PKCE, callback replay, nonce, signature, expiry, cancellation, Google redirects and automatic credential renewal. They do not prove that a deployed identity provider accepts the requests.

The `native.authentication.oidc_live_zitadel_*` tests exercise the same PodLord login implementation against a real ZITADEL server. They cover device authorization, an unknown client, browser PKCE, completed device login with an access token, completed device login with a verified ID token, and completed browser login through the PodLord callback. No Kubernetes request is sent. CTest skips these tests when the required environment is absent.

1. Start a disposable local ZITADEL using its [official Docker Compose instructions](https://zitadel.com/docs/self-hosting/deploy/compose). The verified local run used ZITADEL `v4.16.0` and the matching Compose example. Give the first instance a local machine account with `IAM_OWNER` and a PAT. Keep its PAT in a private local file.
2. Create a native OIDC application in a test project. Enable authorization code with PKCE and device code. Allow `http://localhost:8427` as a redirect. The application must have no client secret. Keep the generated client ID in local test configuration.
3. Make a local test user with a password. Keep the login-client PAT in a separate private file. The full tests use that PAT to create and approve a session through ZITADEL's documented APIs.
4. Set `PODLORD_ZITADEL_ISSUER`, `PODLORD_ZITADEL_CLIENT_ID`, `PODLORD_ZITADEL_LOGIN_PAT_FILE`, `PODLORD_ZITADEL_TEST_USERNAME` and `PODLORD_ZITADEL_TEST_PASSWORD` in the test process environment. Then run:

   ```sh
   ctest --test-dir /path/to/native-build --output-on-failure -R '^native.authentication.oidc_live_zitadel_'
   ```

Stop the disposable stack and remove its owned volumes after testing. Do not use a production identity provider, real user or shared client for this fixture. Google protocol cases remain in the normal suite. A live Google consent test needs a registered test OAuth client and test account, so it is not part of this local fixture.
