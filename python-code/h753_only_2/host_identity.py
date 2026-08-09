from pqcrypto.sign.ml_dsa_44 import generate_keypair, sign, verify

PUBLIC_KEY_SIZE = 1312
SECRET_KEY_SIZE = 2560
SIGNATURE_SIZE = 2420


class HostIdentity:
    """
    Host（Python）自己的 ML-DSA-44 身分，完全在本地算，不碰 UART。

    跟 library/mldsa_app.py 的 STM32MLDSA 不一樣 —— 那個是
    「請 STM32 幫你簽/驗」，這個是「Python 自己簽」。用在 ML-KEM
    handshake 的雙向認證：

      - HOST_SIGNS 模式：用這裡的 secret key 對 kem_ciphertext 簽章，
        送給 STM32 驗證。STM32 要先透過 CMD_SET_HOST_MLDSA_PUBKEY
        知道這把 public key 才能驗證（TOFU 模型 —— trust on first use，
        沒有憑證鏈，STM32 送什麼就信什麼，這是 demo/教學專案的簡化，
        正式產品要換成有根憑證或別的方式建立信任）。

    驗證 STM32 簽章（DEVICE_SIGNS 模式）不需要這個 class 的實例，
    直接用這個檔案的 verify_signature() 函式配 STM32 的 public key
    （從 STM32MLDSA.get_public_key() 拿）就好。
    """

    def __init__(self):
        self.public_key, self.secret_key = generate_keypair()

        if len(self.public_key) != PUBLIC_KEY_SIZE:
            raise RuntimeError(f"Unexpected public key size: {len(self.public_key)}")

    def sign(self, message: bytes) -> bytes:
        if not isinstance(message, (bytes, bytearray)):
            raise TypeError("message must be bytes")

        signature = sign(self.secret_key, bytes(message))

        if len(signature) != SIGNATURE_SIZE:
            raise RuntimeError(f"Unexpected signature size: {len(signature)}")

        return signature


def verify_signature(public_key: bytes, message: bytes, signature: bytes) -> bool:
    """
    純本地驗證，不碰 UART。可以用來驗 STM32 的簽章（DEVICE_SIGNS 模式，
    STM32 的 public key 另外從 STM32MLDSA.get_public_key() 拿），
    也可以驗任何一把 ML-DSA-44 public key 對應的簽章。

    已經用實際編出來的 STM32 端 ML-DSA-44 程式碼跟這個套件做過雙向
    簽章互驗證，證實兩邊演算法完全相容（同一套 FIPS 204 標準）。

    注意：pqcrypto 的 verify() 驗證失敗時是回傳 False，不是丟例外
    （這點容易搞錯——如果誤用「try/except 抓例外」來判斷，驗證失敗時
    因為根本不會有例外，函式會永遠回傳 True，等於認證形同虛設）。
    這裡同時處理兩種可能：正常情況讀回傳值，如果傳入的 key/signature
    格式有問題導致底層丟例外，也會被安全地當成「驗證失敗」處理。
    """
    try:
        return bool(verify(public_key, message, signature))
    except Exception:
        return False
