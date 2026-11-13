"""Optional black-box test against a running native-sim EF800Application.

Set EF800_SIMULATOR_HOST to the private simulator TCP endpoint. The simulator
must run the production EF800Application image with its MTP personality active.
"""
import asyncio
import os
import unittest
import uuid

from gateway import Gateway
from protocol import MSG_KEY_DOWN, MSG_KEY_UP


@unittest.skipUnless(os.getenv("EF800_SIMULATOR_HOST"),
                     "set EF800_SIMULATOR_HOST to run native-sim MTP E2E")
class MainMtpGatewayEndToEndTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.gateway = Gateway(
            os.environ["EF800_SIMULATOR_HOST"],
            int(os.getenv("EF800_SIMULATOR_PORT", "8800")))
        self.gateway_task = asyncio.create_task(self.gateway.simulator_loop())
        deadline = asyncio.get_running_loop().time() + 15
        while not self.gateway.simulator_connected:
            if asyncio.get_running_loop().time() >= deadline:
                self.fail("native-sim did not connect to its private gateway endpoint")
            await asyncio.sleep(0.05)
        await asyncio.sleep(0.2)

    async def asyncTearDown(self):
        self.gateway.running = False
        self.gateway_task.cancel()
        try:
            await self.gateway_task
        except asyncio.CancelledError:
            pass

    async def test_gateway_file_operations_round_trip_through_main_mtp(self):
        async def press(key):
            encoded = key.encode("ascii")
            await self.gateway.send_simulator(MSG_KEY_DOWN, encoded)
            await asyncio.sleep(0.08)
            await self.gateway.send_simulator(MSG_KEY_UP, encoded)
            await asyncio.sleep(0.08)

        # Select the actual Port 4 USB MTP personality through EF800Application
        # rather than making the host peer talk to an inactive/stub class.
        for key in ("e", "p", "4", "m"):
            await press(key)
        for key in ("down", "down", "enter"):
            await press(key)
        await asyncio.sleep(0.25)

        payload = bytes((index * 29) & 0xFF for index in range(5001))
        filename = f"gateway-mtp-{uuid.uuid4().hex[:12]}.bin"
        upload = await self.gateway.mtp_upload("Data", "", filename, payload)
        self.assertTrue(upload["accepted"])

        listing = await self.gateway.mtp_list("Data", "")
        self.assertIn(filename, {entry["name"] for entry in listing["entries"]})
        downloaded = await self.gateway.mtp_download("Data", filename)
        self.assertEqual(downloaded, payload)
